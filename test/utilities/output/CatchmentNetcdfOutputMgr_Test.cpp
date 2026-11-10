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

#include "gtest/gtest.h"

#include <CatchmentNetcdfOutputMgr.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#include <netcdf.h>

// Unit tests for the NetCDF catchment output backend. Each test writes a real file and reads it back
// with the netCDF-C API: dimensions, catchment ids, time values, variable values and attributes.

namespace {
    // epoch_time 3600 s -> 60 minutes since 1970-01-01, the time encoding the file uses.
    utils::time_marker marker(long index, time_t epoch_seconds) {
        return utils::time_marker(index, epoch_seconds, "unused");
    }

    utils::OutputField col(const std::string& name, std::optional<std::string> units = std::nullopt) {
        return {name, name, std::move(units)};
    }

    std::filesystem::path fresh_dir() {
        static int counter = 0;
        std::filesystem::path d = std::filesystem::temp_directory_path()
            / ("ngen_cat_nc_test_" + std::to_string(getpid())) / std::to_string(counter++);
        std::filesystem::remove_all(d);
        std::filesystem::create_directories(d);
        return d;
    }

    // Read-only view of a NetCDF file for assertions; closes the file on destruction.
    struct NcReader {
        int id = -1;
        explicit NcReader(const std::filesystem::path& p) {
            if (nc_open(p.c_str(), NC_NOWRITE, &id) != NC_NOERR) {
                throw std::runtime_error("cannot open " + p.string());
            }
        }
        ~NcReader() { if (id != -1) nc_close(id); }

        std::size_t dim(const std::string& name) const {
            int dim_id;
            std::size_t len;
            if (nc_inq_dimid(id, name.c_str(), &dim_id) != NC_NOERR || nc_inq_dimlen(id, dim_id, &len) != NC_NOERR) {
                throw std::runtime_error("no dimension " + name);
            }
            return len;
        }
        int var(const std::string& name) const {
            int var_id;
            if (nc_inq_varid(id, name.c_str(), &var_id) != NC_NOERR) throw std::runtime_error("no variable " + name);
            return var_id;
        }
        std::vector<std::string> ids() const {
            const std::size_t n = dim("feature_id"), w = dim("feature_id_str_len");
            std::vector<char> buf(n * w);
            nc_get_var_text(id, var("feature_id"), buf.data());
            std::vector<std::string> out;
            for (std::size_t i = 0; i < n; ++i) out.emplace_back(buf.data() + i * w, strnlen(buf.data() + i * w, w));
            return out;
        }
        std::vector<long long> times() const {
            std::vector<long long> out(dim("time"));
            nc_get_var_longlong(id, var("time"), out.data());
            return out;
        }
        //! Values of a (feature_id, time) variable, row-major: [catchment * n_time + time].
        std::vector<double> values(const std::string& name) const {
            std::vector<double> out(dim("feature_id") * dim("time"));
            nc_get_var_double(id, var(name), out.data());
            return out;
        }
        bool has_attribute(const std::string& var_name, const std::string& att) const {
            int att_id;
            return nc_inq_attid(id, var(var_name), att.c_str(), &att_id) == NC_NOERR;
        }
        std::optional<std::string> text_attribute(const std::string& var_name, const std::string& att) const {
            std::size_t len;
            if (nc_inq_attlen(id, var(var_name), att.c_str(), &len) != NC_NOERR) return std::nullopt;
            std::string out(len, '\0');
            nc_get_att_text(id, var(var_name), att.c_str(), out.data());
            return out;
        }
    };
}

// Two catchments, two time steps: ids, time (minutes since epoch), values on (feature_id, time),
// units only on the field that has them, and source_name on every variable.
TEST(CatchmentNetcdfOutputMgr_Test, WritesIdsTimeValuesAndUnits)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("Q_OUT", "m"), col("SOIL_STORAGE")}},
              {"cat-27", {col("Q_OUT", "m"), col("SOIL_STORAGE")}} });
        mgr.receive_data_entry("cat-1", marker(0, 3600), {1.5, 10.0});
        mgr.receive_data_entry("cat-27", marker(0, 3600), {2.5, 20.0});
        mgr.receive_data_entry("cat-1", marker(1, 7200), {3.5, 30.0});
        mgr.receive_data_entry("cat-27", marker(1, 7200), {4.5, 40.0});
        mgr.close();
        EXPECT_TRUE(mgr.is_closed());
    }

    NcReader nc(dir / "cat_output.nc");
    EXPECT_EQ(nc.ids(), (std::vector<std::string>{"cat-1", "cat-27"}));
    EXPECT_EQ(nc.times(), (std::vector<long long>{60, 120}));
    EXPECT_EQ(nc.values("Q_OUT"), (std::vector<double>{1.5, 3.5, 2.5, 4.5}));
    EXPECT_EQ(nc.values("SOIL_STORAGE"), (std::vector<double>{10.0, 30.0, 20.0, 40.0}));
    EXPECT_EQ(nc.text_attribute("Q_OUT", "units"), std::optional<std::string>("m"));
    EXPECT_EQ(nc.text_attribute("SOIL_STORAGE", "units"), std::nullopt);   // the model reported none
    EXPECT_EQ(nc.text_attribute("Q_OUT", "source_name"), std::optional<std::string>("Q_OUT"));
    EXPECT_EQ(nc.text_attribute("time", "units"), std::optional<std::string>("minutes since 1970-01-01 00:00:00"));
    EXPECT_TRUE(nc.has_attribute("Q_OUT", "_FillValue"));

    std::filesystem::remove_all(dir);
}

// An aliased field is written under its output name, with the source name as an attribute, and its
// extra attributes are carried over.
TEST(CatchmentNetcdfOutputMgr_Test, AliasAndAttributes)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {utils::OutputField("land_surface_water__runoff_depth", "runoff", std::string("m h-1"),
                                            {{"long_name", "Runoff"}})}} });
        mgr.receive_data_entry("cat-1", marker(0, 0), {0.25});
        mgr.close();
    }

    NcReader nc(dir / "cat_output.nc");
    EXPECT_EQ(nc.values("runoff"), (std::vector<double>{0.25}));
    EXPECT_EQ(nc.text_attribute("runoff", "source_name"),
              std::optional<std::string>("land_surface_water__runoff_depth"));
    EXPECT_EQ(nc.text_attribute("runoff", "units"), std::optional<std::string>("m h-1"));
    EXPECT_EQ(nc.text_attribute("runoff", "long_name"), std::optional<std::string>("Runoff"));

    std::filesystem::remove_all(dir);
}

// A step that not every catchment reported is still written on close, with NaN for the missing one.
TEST(CatchmentNetcdfOutputMgr_Test, IncompleteStepWrittenWithNaNOnClose)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("Q_OUT")}}, {"cat-2", {col("Q_OUT")}} });
        mgr.receive_data_entry("cat-1", marker(0, 0), {1.0});
        mgr.close();
    }

    NcReader nc(dir / "cat_output.nc");
    const auto q = nc.values("Q_OUT");
    ASSERT_EQ(q.size(), 2u);
    EXPECT_EQ(q[0], 1.0);
    EXPECT_TRUE(std::isnan(q[1]));

    std::filesystem::remove_all(dir);
}

// An entry that arrives after commit_writes() wrote its step incomplete completes that step.
TEST(CatchmentNetcdfOutputMgr_Test, LateEntryAfterCommitCompletesStep)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("Q_OUT")}}, {"cat-2", {col("Q_OUT")}} });
        mgr.receive_data_entry("cat-1", marker(0, 0), {1.0});
        mgr.commit_writes();
        mgr.receive_data_entry("cat-2", marker(0, 0), {2.0});
        mgr.close();
    }

    NcReader nc(dir / "cat_output.nc");
    EXPECT_EQ(nc.values("Q_OUT"), (std::vector<double>{1.0, 2.0}));

    std::filesystem::remove_all(dir);
}

// A step that not every catchment reported is written, with NaN for the missing catchment, when
// the next step starts -- only the current step is buffered.
TEST(CatchmentNetcdfOutputMgr_Test, IncompleteStepWrittenWhenNextStepStarts)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("Q_OUT")}}, {"cat-2", {col("Q_OUT")}} });
        mgr.receive_data_entry("cat-1", marker(0, 60), {1.0});
        mgr.receive_data_entry("cat-1", marker(1, 120), {3.0});
        mgr.receive_data_entry("cat-2", marker(1, 120), {4.0});
        mgr.close();
    }

    NcReader nc(dir / "cat_output.nc");
    EXPECT_EQ(nc.times(), (std::vector<long long>{1, 2}));
    const auto q = nc.values("Q_OUT");   // [cat-1 t0, cat-1 t1, cat-2 t0, cat-2 t1]
    ASSERT_EQ(q.size(), 4u);
    EXPECT_EQ(q[0], 1.0);
    EXPECT_EQ(q[1], 3.0);
    EXPECT_TRUE(std::isnan(q[2]));
    EXPECT_EQ(q[3], 4.0);

    std::filesystem::remove_all(dir);
}

// Time steps must arrive in order: a step earlier than the current one is rejected, not buffered.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnOutOfOrderStep)
{
    const auto dir = fresh_dir();
    utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc", { {"cat-1", {col("Q_OUT")}} });
    mgr.receive_data_entry("cat-1", marker(0, 60), {1.0});
    mgr.receive_data_entry("cat-1", marker(1, 120), {2.0});
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(0, 60), {1.0}), std::runtime_error);
    mgr.close();
    std::filesystem::remove_all(dir);
}

// Skipping a time index, or not starting at 0, would leave a time slot unwritten, so it is rejected.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnSkippedStep)
{
    const auto dir = fresh_dir();
    utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc", { {"cat-1", {col("Q_OUT")}} });
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(1, 120), {2.0}), std::runtime_error);   // not 0
    mgr.receive_data_entry("cat-1", marker(0, 60), {1.0});
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(2, 180), {3.0}), std::runtime_error);   // gap
    mgr.close();
    std::filesystem::remove_all(dir);
}

// Catchment layers with different output time steps each count their own index from 0, so the same
// index arrives with different times. That can't share one time axis and is rejected.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsWhenSameIndexHasDifferentTime)
{
    const auto dir = fresh_dir();
    utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
        { {"cat-1", {col("Q_OUT")}}, {"cat-2", {col("Q_OUT")}} });
    mgr.receive_data_entry("cat-1", marker(0, 0), {1.0});
    mgr.receive_data_entry("cat-2", marker(0, 0), {1.0});
    mgr.receive_data_entry("cat-1", marker(1, 3600), {1.0});    // hourly layer: index 1 = 01:00
    EXPECT_THROW(mgr.receive_data_entry("cat-2", marker(1, 900), {2.0}), std::runtime_error);   // 15-min layer: 00:15
    mgr.close();
    std::filesystem::remove_all(dir);
}

// A present-but-empty units string is kept as an empty attribute, distinct from no units at all.
TEST(CatchmentNetcdfOutputMgr_Test, EmptyUnitsKeptDistinctFromNone)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("ratio", std::string("")), col("other")}} });
        mgr.close();
    }

    NcReader nc(dir / "cat_output.nc");
    EXPECT_EQ(nc.text_attribute("ratio", "units"), std::optional<std::string>(""));
    EXPECT_EQ(nc.text_attribute("other", "units"), std::nullopt);
    EXPECT_EQ(nc.dim("time"), 0u);   // no data pushed

    std::filesystem::remove_all(dir);
}

// Each formulation gets its own file; a non-default formulation goes in its own subdirectory, so
// formulations with different fields never share a file.
TEST(CatchmentNetcdfOutputMgr_Test, SplitsByFormulation)
{
    const auto dir = fresh_dir();
    {
        utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc",
            { {"cat-1", {col("Q_OUT")}},
              {"f2", "cat-1", {col("tci", "m"), col("qs", "mm")}} });
        mgr.receive_data_entry("cat-1", marker(0, 0), {1.0});
        mgr.receive_data_entry("f2", "cat-1", marker(0, 0), {2.0, 3.0});
        mgr.close();
    }

    NcReader def(dir / "cat_output.nc");
    EXPECT_EQ(def.values("Q_OUT"), (std::vector<double>{1.0}));
    NcReader f2(dir / "f2" / "cat_output.nc");
    EXPECT_EQ(f2.values("tci"), (std::vector<double>{2.0}));
    EXPECT_EQ(f2.values("qs"), (std::vector<double>{3.0}));

    std::filesystem::remove_all(dir);
}

// Catchments of one formulation share the file's variables, so different fields are rejected.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnDifferentFieldsInOneFormulation)
{
    const auto dir = fresh_dir();
    EXPECT_THROW(utils::CatchmentNetcdfOutputMgr((dir / "").string(), "cat_output.nc",
                     { {"cat-1", {col("Q_OUT")}}, {"cat-2", {col("tci")}} }),
                 std::runtime_error);
    std::filesystem::remove_all(dir);
}

// A catchment listed twice is rejected.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnDuplicateCatchment)
{
    const auto dir = fresh_dir();
    EXPECT_THROW(utils::CatchmentNetcdfOutputMgr((dir / "").string(), "cat_output.nc",
                     { {"cat-1", {col("Q_OUT")}}, {"cat-1", {col("Q_OUT")}} }),
                 std::runtime_error);
    std::filesystem::remove_all(dir);
}

// A second formulation failing removes the file already created for the first.
TEST(CatchmentNetcdfOutputMgr_Test, FailedConstructorRemovesFilesItCreated)
{
    const auto dir = fresh_dir();
    EXPECT_THROW(utils::CatchmentNetcdfOutputMgr((dir / "").string(), "cat_output.nc",
                     { {"cat-1", {col("Q_OUT")}},                                  // default: created first
                       {"f2", "cat-1", {col("tci")}}, {"f2", "cat-2", {col("qs")}} }),   // f2: fields differ
                 std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(dir / "cat_output.nc"));
    std::filesystem::remove_all(dir);
}

// An output root that can't be created (a regular file is in the way) is reported as an error.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsWhenRootCannotBeCreated)
{
    const auto dir = fresh_dir();
    std::ofstream(dir / "blocker") << "x";
    EXPECT_THROW(utils::CatchmentNetcdfOutputMgr((dir / "blocker" / "sub" / "").string(), "cat_output.nc",
                                                 { {"cat-1", {col("Q_OUT")}} }),
                 std::runtime_error);
    std::filesystem::remove_all(dir);
}

// An output name equal to a coordinate name would collide with it.
TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnCoordinateNameClash)
{
    const auto dir = fresh_dir();
    EXPECT_THROW(utils::CatchmentNetcdfOutputMgr((dir / "").string(), "cat_output.nc", { {"cat-1", {col("time")}} }),
                 std::runtime_error);
    std::filesystem::remove_all(dir);
}

TEST(CatchmentNetcdfOutputMgr_Test, ThrowsOnInvalidEntries)
{
    const auto dir = fresh_dir();
    utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc", { {"cat-1", {col("Q_OUT")}} });
    EXPECT_THROW(mgr.receive_data_entry("cat-9", marker(0, 0), {1.0}), std::runtime_error);
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(0, 0), {1.0, 2.0}), std::runtime_error);
    EXPECT_THROW(mgr.receive_data_entry("f9", "cat-1", marker(0, 0), {1.0}), std::runtime_error);   // formulation
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(-1, 0), {1.0}), std::runtime_error);       // negative index
    mgr.close();
    std::filesystem::remove_all(dir);
}

TEST(CatchmentNetcdfOutputMgr_Test, ThrowsAfterClose)
{
    const auto dir = fresh_dir();
    utils::CatchmentNetcdfOutputMgr mgr((dir / "").string(), "cat_output.nc", { {"cat-1", {col("Q_OUT")}} });
    mgr.close();
    mgr.close();   // idempotent
    EXPECT_THROW(mgr.receive_data_entry("cat-1", marker(0, 0), {1.0}), std::runtime_error);
    std::filesystem::remove_all(dir);
}
