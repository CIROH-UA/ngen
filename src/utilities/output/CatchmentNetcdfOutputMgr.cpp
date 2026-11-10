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

#include "CatchmentNetcdfOutputMgr.hpp"

#if NGEN_WITH_NETCDF

#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <netcdf.h>

using utils::CatchmentNetcdfOutputMgr;

namespace {
    //! Throw a runtime_error naming the failed step and the NetCDF error, unless status is NC_NOERR.
    void check(int status, const std::string &what, const std::filesystem::path &path)
    {
        if (status != NC_NOERR) {
            throw std::runtime_error("CatchmentNetcdfOutputMgr: " + what + " in '" + path.string() + "': "
                                     + nc_strerror(status));
        }
    }

    void put_text_attribute(int nc_id, int var_id, const std::string &name, const std::string &value,
                            const std::filesystem::path &path)
    {
        check(nc_put_att_text(nc_id, var_id, name.c_str(), value.size(), value.c_str()),
              "cannot write attribute '" + name + "'", path);
    }

    //! The output names of a field list, in order, for comparing catchments' schemas.
    std::vector<std::string> output_names(const std::vector<utils::OutputField> &fields)
    {
        std::vector<std::string> names;
        names.reserve(fields.size());
        for (const auto &f : fields) {
            names.push_back(f.output_name);
        }
        return names;
    }
}

CatchmentNetcdfOutputMgr::CatchmentNetcdfOutputMgr(std::string output_root, std::string filename,
                                                   std::vector<FeatureDescriptor> descriptors)
    : output_root_(std::move(output_root))
    , filename_(std::move(filename))
{
    // NC_INT64 time values are written with nc_put_vara_longlong (as the nexus NetCDF writer does).
    static_assert(sizeof(long long) == 8, "NC_INT64 time values need an 8-byte long long");

    // Group the catchments by formulation: each formulation gets its own file.
    std::map<std::string, std::vector<const FeatureDescriptor *>> by_formulation;
    for (const auto &d : descriptors) {
        by_formulation[d.formulation_id].push_back(&d);
    }

    try {
        for (const auto &[formulation_id, features] : by_formulation) {
            File &file = files_[formulation_id];
            file.path = output_path(formulation_id);
            index_catchments(file, features);
            create_file(file, features.front()->fields);
        }
    }
    catch (...) {
        // Don't leave open handles, or half-defined files that could pass for output, behind.
        for (auto &entry : files_) {
            if (entry.second.nc_id != -1) {
                nc_close(entry.second.nc_id);
                std::error_code ignored;
                std::filesystem::remove(entry.second.path, ignored);
            }
        }
        throw;
    }
}

CatchmentNetcdfOutputMgr::~CatchmentNetcdfOutputMgr()
{
    // Best-effort backstop for callers who don't close() explicitly; close() is the path that reports
    // errors, and a destructor must not let one escape, so report it here instead.
    try {
        close();
    } catch (const std::exception &e) {
        std::cout << "ERROR: Could not close catchment NetCDF output during manager destruction: " << e.what()
                  << std::endl;
    } catch (...) {
        std::cout << "ERROR: Could not close catchment NetCDF output during manager destruction." << std::endl;
    }
}

std::filesystem::path CatchmentNetcdfOutputMgr::output_path(const std::string &formulation_id) const
{
    // Same layout as the aggregated CSV backend: the default formulation is flat, any other
    // formulation id gets its own subdirectory.
    if (formulation_id == default_formulation_id()) {
        return output_root_ + filename_;
    }
    return output_root_ + formulation_id + "/" + filename_;
}

void CatchmentNetcdfOutputMgr::index_catchments(File &file, const std::vector<const FeatureDescriptor *> &features)
{
    // One variable per field is shared by every catchment of the file, so the catchments must agree.
    const std::vector<std::string> names = output_names(features.front()->fields);
    for (const FeatureDescriptor *feature : features) {
        if (output_names(feature->fields) != names) {
            throw std::runtime_error("CatchmentNetcdfOutputMgr: catchment '" + feature->catchment_id
                                     + "' has different output fields than '" + features.front()->catchment_id
                                     + "'; the catchments of one formulation share one NetCDF file and must "
                                     + "have the same output fields");
        }
        if (!file.catchment_index.emplace(feature->catchment_id, file.catchment_ids.size()).second) {
            throw std::runtime_error("CatchmentNetcdfOutputMgr: catchment '" + feature->catchment_id
                                     + "' is listed more than once");
        }
        file.catchment_ids.push_back(feature->catchment_id);
    }
    for (const auto &name : names) {
        if (name == FEATURE_ID_DIM || name == FEATURE_ID_STR_LEN_DIM || name == TIME_DIM) {
            throw std::runtime_error("CatchmentNetcdfOutputMgr: output variable '" + name
                                     + "' has the same name as a coordinate of the NetCDF file");
        }
    }
}

void CatchmentNetcdfOutputMgr::create_file(File &file, const std::vector<OutputField> &fields)
{
    const auto &path = file.path;
    const std::size_t n_cat = file.catchment_ids.size();
    std::size_t id_width = 1;
    for (const auto &id : file.catchment_ids) {
        id_width = std::max(id_width, id.size());
    }

    if (const auto dir = path.parent_path(); !dir.empty()) {
        std::filesystem::create_directories(dir);
    }
    // NC_CLOBBER: a rerun overwrites the file, as the CSV backend truncates its files.
    check(nc_create(path.c_str(), NC_NETCDF4 | NC_CLOBBER, &file.nc_id), "cannot create file", path);
    const int nc_id = file.nc_id;

    int feature_dim, str_len_dim, time_dim;
    check(nc_def_dim(nc_id, FEATURE_ID_DIM, n_cat, &feature_dim),
          std::string("cannot define dimension '") + FEATURE_ID_DIM + "'", path);
    check(nc_def_dim(nc_id, TIME_DIM, NC_UNLIMITED, &time_dim),
          std::string("cannot define dimension '") + TIME_DIM + "'", path);
    check(nc_def_dim(nc_id, FEATURE_ID_STR_LEN_DIM, id_width, &str_len_dim),
          std::string("cannot define dimension '") + FEATURE_ID_STR_LEN_DIM + "'", path);

    int feature_var;
    const int feature_var_dims[] = {feature_dim, str_len_dim};
    check(nc_def_var(nc_id, FEATURE_ID_DIM, NC_CHAR, 2, feature_var_dims, &feature_var),
          std::string("cannot define variable '") + FEATURE_ID_DIM + "'", path);
    put_text_attribute(nc_id, feature_var, "long_name", "Feature ID", path);

    check(nc_def_var(nc_id, TIME_DIM, NC_INT64, 1, &time_dim, &file.time_var_id),
          std::string("cannot define variable '") + TIME_DIM + "'", path);
    put_text_attribute(nc_id, file.time_var_id, "units", TIME_UNITS, path);
    put_text_attribute(nc_id, file.time_var_id, "calendar", "gregorian", path);
    put_text_attribute(nc_id, file.time_var_id, "long_name", "Time", path);

    // A chunk holds every catchment (each write covers one time step of all of them) and as many time
    // steps as keep it near CHUNK_TARGET_VALUES: one-step chunks are tiny when a rank has few
    // catchments, and HDF5's per-chunk overhead then outweighs the data. The partly filled chunk stays
    // in HDF5's chunk cache between writes. Shuffle + deflate make the files smaller than the CSVs.
    const int value_dims[] = {feature_dim, time_dim};
    const std::size_t chunk_catchments = std::max<std::size_t>(n_cat, 1);
    const std::size_t chunk_steps = std::clamp<std::size_t>(CHUNK_TARGET_VALUES / chunk_catchments, 1, CHUNK_MAX_STEPS);
    const std::size_t chunk[] = {chunk_catchments, chunk_steps};
    const double fill = std::numeric_limits<double>::quiet_NaN();
    for (const OutputField &field : fields) {
        int var_id;
        check(nc_def_var(nc_id, field.output_name.c_str(), NC_DOUBLE, 2, value_dims, &var_id),
              "cannot define variable '" + field.output_name + "'", path);
        check(nc_def_var_chunking(nc_id, var_id, NC_CHUNKED, chunk),
              "cannot set chunking for variable '" + field.output_name + "'", path);
        check(nc_def_var_deflate(nc_id, var_id, 1, 1, DEFLATE_LEVEL),
              "cannot set compression for variable '" + field.output_name + "'", path);
        check(nc_def_var_fill(nc_id, var_id, NC_FILL, &fill),
              "cannot set the fill value of variable '" + field.output_name + "'", path);
        // Extra attributes first, so the units and source_name taken from the model are what remain.
        for (const auto &[name, value] : field.attributes) {
            put_text_attribute(nc_id, var_id, name, value, path);
        }
        if (field.units.has_value()) {
            put_text_attribute(nc_id, var_id, "units", *field.units, path);
        }
        put_text_attribute(nc_id, var_id, "source_name", field.source_name, path);
        file.var_ids.push_back(var_id);
    }

    check(nc_enddef(nc_id), "cannot leave define mode", path);

    // The catchment ids never change, so write them now: fixed-width, null-padded records.
    if (n_cat > 0) {
        std::vector<char> packed(n_cat * id_width, '\0');
        for (std::size_t i = 0; i < n_cat; ++i) {
            const auto &id = file.catchment_ids[i];
            std::copy(id.begin(), id.end(), packed.begin() + static_cast<std::ptrdiff_t>(i * id_width));
        }
        const std::size_t start[] = {0, 0};
        const std::size_t count[] = {n_cat, id_width};
        check(nc_put_vara_text(nc_id, feature_var, start, count, packed.data()),
              "cannot write the catchment ids", path);
    }
}

void CatchmentNetcdfOutputMgr::receive_data_entry(const std::string &formulation_id, const std::string &catchment_id,
                                                  const time_marker &data_time_marker,
                                                  const std::vector<double> &values)
{
    std::lock_guard lock(mutex_);
    if (closed_) {
        throw std::runtime_error("Can't receive data on a closed CatchmentNetcdfOutputMgr");
    }
    const auto file_it = files_.find(formulation_id);
    if (file_it == files_.end()) {
        throw std::runtime_error("CatchmentNetcdfOutputMgr received data for an unknown formulation '"
                                 + formulation_id + "'");
    }
    File &file = file_it->second;
    const auto cat_it = file.catchment_index.find(catchment_id);
    if (cat_it == file.catchment_index.end()) {
        throw std::runtime_error("CatchmentNetcdfOutputMgr received data for an unknown catchment '"
                                 + catchment_id + "'");
    }
    if (values.size() != file.var_ids.size()) {
        throw std::runtime_error("CatchmentNetcdfOutputMgr received " + std::to_string(values.size())
                                 + " values for catchment '" + catchment_id + "', expected "
                                 + std::to_string(file.var_ids.size()));
    }

    const long index = data_time_marker.sim_time_index;
    const long long epoch_minutes = static_cast<long long>(data_time_marker.epoch_time) / 60;
    if (index < 0) {
        throw std::runtime_error("CatchmentNetcdfOutputMgr received a negative time index for catchment '"
                                 + catchment_id + "'");
    }
    // Steps arrive in order, without gaps (ngen's layers count 0, 1, 2, ...), so every time slot of
    // the file is written. Layers with different output time steps break this: each counts its own index.
    if (index != file.step.index && index != file.step.index + 1) {
        throw std::runtime_error("CatchmentNetcdfOutputMgr received time index " + std::to_string(index)
                                 + " for catchment '" + catchment_id + "' while at time index "
                                 + std::to_string(file.step.index) + "; time steps must arrive in order from 0 "
                                 + "without gaps. NetCDF catchment output needs every catchment layer on the "
                                 + "same output time step (CSV output does not)");
    }
    if (index == file.step.index + 1) {
        start_step(file, index, epoch_minutes);
    }
    else if (epoch_minutes != file.step.epoch_minutes) {
        // Same index, different time: catchment layers with different output time steps each count
        // their own index, so their entries can't share this file's time axis.
        throw std::runtime_error("CatchmentNetcdfOutputMgr: catchment '" + catchment_id + "' reported time index "
                                 + std::to_string(index) + " at " + std::to_string(epoch_minutes)
                                 + " minutes since 1970, but that index is at "
                                 + std::to_string(file.step.epoch_minutes) + "; NetCDF catchment output needs "
                                 + "every catchment layer on the same output time step (CSV output does not)");
    }

    Step &step = file.step;
    const std::size_t n_cat = file.catchment_ids.size();
    const std::size_t c = cat_it->second;
    for (std::size_t v = 0; v < values.size(); ++v) {
        step.values[v * n_cat + c] = values[v];
    }
    if (!step.received[c]) {
        step.received[c] = true;
        ++step.received_count;
    }
    step.written = false;
    // Every catchment has reported this step: write it now (a repeated entry rewrites it).
    if (step.received_count == n_cat) {
        write_step(file);
    }
}

void CatchmentNetcdfOutputMgr::warn_if_incomplete(File &file)
{
    const Step &step = file.step;
    const std::size_t n_cat = file.catchment_ids.size();
    if (step.index >= 0 && !step.written && step.received_count < n_cat && !file.warned_incomplete) {
        std::cout << "WARN: catchment NetCDF output '" << file.path.string() << "': only "
                  << step.received_count << " of " << n_cat << " catchments reported time index "
                  << step.index << "; the others are written as NaN (warning shown once per file)."
                  << std::endl;
        file.warned_incomplete = true;
    }
}

void CatchmentNetcdfOutputMgr::start_step(File &file, long index, long long epoch_minutes)
{
    Step &step = file.step;
    const std::size_t n_cat = file.catchment_ids.size();
    warn_if_incomplete(file);
    write_step(file);
    step.index = index;
    step.epoch_minutes = epoch_minutes;
    step.values.assign(file.var_ids.size() * n_cat, std::numeric_limits<double>::quiet_NaN());
    step.received.assign(n_cat, false);
    step.received_count = 0;
    step.written = false;
}

void CatchmentNetcdfOutputMgr::write_step(File &file)
{
    Step &step = file.step;
    if (step.written) {
        return;
    }
    const std::size_t n_cat = file.catchment_ids.size();
    const std::size_t t = static_cast<std::size_t>(step.index);
    const std::size_t one = 1;
    check(nc_put_vara_longlong(file.nc_id, file.time_var_id, &t, &one, &step.epoch_minutes),
          "cannot write time index " + std::to_string(step.index), file.path);
    const std::size_t start[] = {0, t};
    const std::size_t count[] = {n_cat, 1};
    for (std::size_t v = 0; v < file.var_ids.size(); ++v) {
        check(nc_put_vara_double(file.nc_id, file.var_ids[v], start, count, step.values.data() + v * n_cat),
              "cannot write values at time index " + std::to_string(step.index), file.path);
    }
    step.written = true;
}

void CatchmentNetcdfOutputMgr::commit_writes()
{
    std::lock_guard lock(mutex_);
    if (closed_) {
        return;
    }
    for (auto &entry : files_) {
        write_step(entry.second);
        check(nc_sync(entry.second.nc_id), "cannot sync", entry.second.path);
    }
}

void CatchmentNetcdfOutputMgr::close()
{
    std::lock_guard lock(mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    // close() == commit + finalize. Close every file even when one fails, then report the first error.
    std::exception_ptr first_error;
    for (auto &entry : files_) {
        File &file = entry.second;
        try {
            warn_if_incomplete(file);
            write_step(file);
        } catch (...) {
            if (!first_error) first_error = std::current_exception();
        }
        const int status = nc_close(file.nc_id);
        file.nc_id = -1;
        if (status != NC_NOERR && !first_error) {
            try {
                check(status, "cannot close file", file.path);
            } catch (...) {
                first_error = std::current_exception();
            }
        }
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

bool CatchmentNetcdfOutputMgr::is_closed()
{
    std::lock_guard lock(mutex_);
    return closed_;
}

#endif // NGEN_WITH_NETCDF
