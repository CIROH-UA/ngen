/*
Copyright (C) 2026 Aquaveo
------------------------------------------------------------------------
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
------------------------------------------------------------------------
*/

#ifndef NGEN_CATCHMENTNETCDFOUTPUTMGR_HPP
#define NGEN_CATCHMENTNETCDFOUTPUTMGR_HPP

#include <NGenConfig.h>

#if NGEN_WITH_NETCDF

#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "CatchmentOutputsMgr.hpp"

namespace utils
{
    /**
     * NetCDF backend for catchment output: the catchments of each formulation share one NetCDF-4
     * file, laid out like the nexus NetCDF output (@ref PerFormulationNexusOutputMgr):
     *
     *   dimensions: feature_id = <catchments>, time = UNLIMITED, feature_id_str_len = <longest id>
     *   char   feature_id(feature_id, feature_id_str_len)   the full catchment id, e.g. "cat-27"
     *   int64  time(time)                                   minutes since 1970-01-01 00:00:00
     *   double <output_name>(feature_id, time)              one per OutputField, NaN fill,
     *                                                       chunked (all catchments x N steps), shuffle + deflate
     *
     * Each variable carries the field's @c units (only when the source reports units, so an empty
     * units string is kept as an empty attribute), its @c source_name, and any extra @c attributes
     * from its OutputField. The default formulation id writes "<output_root><filename>"; any other
     * formulation id writes "<output_root><formulation_id>/<filename>", as the aggregated CSV
     * backend does.
     *
     * Time steps must arrive in order from index 0 without gaps (as the simulation delivers them), and
     * all entries for one time index must carry the same epoch time. Each file buffers only the current step: it is
     * written as soon as every catchment has reported it, or when the next step starts (catchments
     * that did not report it stay NaN, with a warning). A step earlier than the current one, or an
     * entry whose epoch time differs from its step's, is rejected -- which is what catchment layers
     * with different output time steps would produce, since each layer counts its own time index.
     * Time values are whole minutes (epoch seconds / 60), like the nexus NetCDF output.
     *
     * The catchments of one formulation must share the same output fields (one variable each), as
     * the aggregated CSV backend assumes for its shared header.
     */
    class CatchmentNetcdfOutputMgr : public CatchmentOutputsMgr
    {
    public:
        static constexpr const char *FEATURE_ID_DIM = "feature_id";
        static constexpr const char *FEATURE_ID_STR_LEN_DIM = "feature_id_str_len";
        static constexpr const char *TIME_DIM = "time";
        static constexpr const char *TIME_UNITS = "minutes since 1970-01-01 00:00:00";
        //! Values per chunk to aim for (32768 doubles = 256 KiB); a chunk is all catchments x N steps.
        static constexpr std::size_t CHUNK_TARGET_VALUES = 32768;
        //! Upper limit on the time steps in one chunk.
        static constexpr std::size_t CHUNK_MAX_STEPS = 1024;
        //! zlib level for the value variables (with the shuffle filter); 1-2 is fast with most of the gain.
        static constexpr int DEFLATE_LEVEL = 2;

        /**
         * @param output_root The directory to write into, with a trailing slash (see
         *        CatchmentCsvOutputMgr). Created here, with any per-formulation subdirectories.
         * @param filename The file name for each formulation's file, e.g. "cat_output.nc".
         * @param descriptors The full set of catchments and their fields. Every file is created and
         *        its dimensions, variables and catchment ids are written here.
         * @throw std::runtime_error if a formulation's catchments have different output fields, a
         *        catchment is listed twice, an output name clashes with a coordinate name, or a
         *        NetCDF call fails. Files created before the failure are removed.
         */
        CatchmentNetcdfOutputMgr(std::string output_root, std::string filename,
                                 std::vector<FeatureDescriptor> descriptors);

        ~CatchmentNetcdfOutputMgr() override;

        CatchmentNetcdfOutputMgr(const CatchmentNetcdfOutputMgr &) = delete;
        CatchmentNetcdfOutputMgr &operator=(const CatchmentNetcdfOutputMgr &) = delete;

        // Keep the default-formulation-id convenience overload visible alongside the override.
        using CatchmentOutputsMgr::receive_data_entry;

        /**
         * @throw std::runtime_error for an unknown formulation or catchment, the wrong number of
         *        values, a negative or out-of-order time index, an epoch time that differs from
         *        the step's, or a closed manager.
         */
        void receive_data_entry(const std::string &formulation_id, const std::string &catchment_id,
                                const time_marker &data_time_marker, const std::vector<double> &values) override;

        void commit_writes() override;

        void close() override;

        bool is_closed() override;

    private:
        //! The time step a file is currently filling.
        struct Step
        {
            long index = -1;                 //!< sim_time_index; -1 before the first entry
            long long epoch_minutes = 0;
            std::vector<double> values;      //!< [variable * catchment_count + catchment], NaN-filled
            std::vector<bool> received;      //!< per catchment
            std::size_t received_count = 0;
            bool written = true;             //!< false while it holds values not yet in the file
        };

        //! One NetCDF file: the catchments of one formulation.
        struct File
        {
            std::filesystem::path path;
            int nc_id = -1;
            int time_var_id = -1;
            std::vector<int> var_ids;                              //!< one per output field
            std::vector<std::string> catchment_ids;
            std::unordered_map<std::string, std::size_t> catchment_index;
            Step step;
            bool warned_incomplete = false;
        };

        const std::string output_root_;
        const std::string filename_;
        bool closed_ = false;
        std::mutex mutex_;                                         //!< serializes this instance's calls
        std::map<std::string, File> files_;                        //!< keyed by formulation id

        std::filesystem::path output_path(const std::string &formulation_id) const;
        //! Check one formulation's catchments can share a file and fill file's catchment ids/index.
        static void index_catchments(File &file, const std::vector<const FeatureDescriptor *> &features);
        //! Create one file and define its dimensions, variables and catchment ids.
        static void create_file(File &file, const std::vector<OutputField> &fields);
        //! Warn (once per file) when the current step is about to be written with catchments missing.
        static void warn_if_incomplete(File &file);
        //! Start a new step in file, writing the current one first if it holds unwritten values.
        static void start_step(File &file, long index, long long epoch_minutes);
        //! Write file's current step (values and time) if it holds unwritten values.
        static void write_step(File &file);
    };
} // utils

#endif // NGEN_WITH_NETCDF

#endif // NGEN_CATCHMENTNETCDFOUTPUTMGR_HPP
