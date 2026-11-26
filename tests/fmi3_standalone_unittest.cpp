/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#define BOOST_TEST_MODULE FMI 3 standalone Co-Simulation
#include <cosim/fmi/v3/fmu.hpp>

#include "cosim/utility/filesystem.hpp"
#include "cosim/utility/zip.hpp"

#include <boost/test/unit_test.hpp>
#include <gsl/span>
#include <gsl/span_ext>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

using namespace cosim;

namespace
{
filesystem::path fixture_path(const char* name)
{
    const auto* data = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(data != nullptr);
    return filesystem::path(data) / "fmi3" / name;
}

std::shared_ptr<fmi::v3::fmu> import_fmu(const char* fixture)
{
    const auto importer = fmi::importer::create();
    const auto imported = importer->import(fixture_path(fixture));
    BOOST_CHECK(imported->fmi_version() == fmi::fmi_version::v3_0);
    const auto model = std::dynamic_pointer_cast<fmi::v3::fmu>(imported);
    BOOST_REQUIRE(model);
    return model;
}

value_reference find_reference(const fmi::v3::fmu& model, const char* name, variable_type type)
{
    const auto description = model.model_description();
    const auto variable = std::find_if(description->variables.begin(), description->variables.end(),
        [name](const auto& candidate) { return candidate.name == name; });
    BOOST_REQUIRE(variable != description->variables.end());
    BOOST_TEST(variable->type == type);
    return variable->reference;
}
} // namespace

BOOST_AUTO_TEST_CASE(archive_and_unpacked_import)
{
    utility::temp_dir directory;
    const auto archivePath = fixture_path("Stair.fmu");
    const utility::zip::archive archive(archivePath);
    archive.extract_all(directory.path());

    const auto importer = fmi::importer::create();
    const auto packed = importer->import(archivePath);
    const auto unpacked = importer->import_unpacked(directory.path());
    BOOST_CHECK(unpacked->fmi_version() == fmi::fmi_version::v3_0);
    BOOST_CHECK(packed != unpacked);
    BOOST_CHECK_EQUAL(packed->model_description()->instantiation_token,
        unpacked->model_description()->instantiation_token);
    BOOST_CHECK_EQUAL(unpacked->model_description()->name, "Stair");
    BOOST_CHECK(importer->import(archivePath) == packed);
    BOOST_CHECK(importer->import_unpacked(directory.path()) == unpacked);

    utility::temp_dir copies;
    const auto copyPath = copies.path() / "Stair-copy.fmu";
    filesystem::copy_file(archivePath, copyPath);
    BOOST_CHECK(importer->import(copyPath) == packed);
}

BOOST_AUTO_TEST_CASE(bouncing_ball_variable_metadata)
{
    const auto model = import_fmu("BouncingBall.fmu");
    const auto description = model->model_description();
    const auto height = std::find_if(description->variables.begin(), description->variables.end(),
        [](const auto& variable) { return variable.name == "h"; });
    BOOST_REQUIRE(height != description->variables.end());
    BOOST_CHECK(height->type == variable_type::real);
    BOOST_REQUIRE(height->declared_type);
    BOOST_CHECK_EQUAL(*height->declared_type, "Position");
    BOOST_REQUIRE(height->unit);
    BOOST_CHECK_EQUAL(*height->unit, "m");
    BOOST_REQUIRE_EQUAL(height->aliases.size(), 1);
    BOOST_CHECK_EQUAL(height->aliases.front().name, "h_ft");
    BOOST_REQUIRE(height->aliases.front().display_unit);
    BOOST_CHECK_EQUAL(*height->aliases.front().display_unit, "ft");
    BOOST_REQUIRE(height->output_dependencies);
    BOOST_CHECK(height->output_dependencies->empty());

    const auto& derivatives = description->structure.continuous_state_derivatives;
    BOOST_REQUIRE_EQUAL(derivatives.size(), 2);
    BOOST_REQUIRE(derivatives.front().dependencies);
    BOOST_REQUIRE_EQUAL(derivatives.front().dependencies->size(), 1);
    const auto& dependency = derivatives.front().dependencies->front();
    BOOST_CHECK_EQUAL(dependency.variable.reference, find_reference(*model, "v", variable_type::real));
    BOOST_CHECK(dependency.kind == dependency_kind::constant);
}

BOOST_AUTO_TEST_CASE(bouncing_ball_without_external_event_mode)
{
    const auto model = import_fmu("BouncingBall.fmu");
    const auto heightRef = find_reference(*model, "h", variable_type::real);
    const auto velocityRef = find_reference(*model, "v", variable_type::real);
    fmi::v3::co_simulation_callbacks options;
    options.event_mode_used = false;
    const auto instance = model->instantiate_v3_slave("ball", options);
    BOOST_CHECK(!instance->event_mode_used());
    const time_point start{};
    const auto stop = to_time_point(0.6);
    instance->setup(start, stop, std::nullopt);
    instance->start_simulation();

    auto now = start;
    while (now < stop) {
        const auto step = std::min(to_duration(0.05), stop - now);
        const auto result = instance->do_step_with_info(now, step);
        BOOST_REQUIRE(result.result == step_result::complete);
        BOOST_REQUIRE(!result.early_return);
        BOOST_REQUIRE(!result.event_handling_needed);
        BOOST_REQUIRE(!result.termination_requested);
        now += step;
    }
    double height = 0.0;
    double velocity = 0.0;
    instance->get_real_variables(gsl::make_span(&heightRef, 1), gsl::make_span(&height, 1));
    instance->get_real_variables(gsl::make_span(&velocityRef, 1), gsl::make_span(&velocity, 1));
    BOOST_CHECK_GT(height, 0.0);
    BOOST_CHECK_GT(velocity, 0.0);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(stair_with_external_event_mode_and_early_return)
{
    const auto model = import_fmu("Stair.fmu");
    const auto counterRef = find_reference(*model, "counter", variable_type::integer);
    fmi::v3::co_simulation_callbacks options;
    options.event_mode_used = true;
    options.early_return_allowed = true;
    const auto instance = model->instantiate_v3_slave("stair", options);
    BOOST_REQUIRE(instance->event_mode_used());
    BOOST_REQUIRE(instance->early_return_allowed());
    instance->setup(time_point{}, to_time_point(3.0), std::nullopt);
    instance->start_simulation();
    instance->handle_events();

    const auto getCounter = [&] {
        int value = 0;
        instance->get_integer_variables(gsl::make_span(&counterRef, 1), gsl::make_span(&value, 1));
        return value;
    };
    auto now = time_point{};
    for (int event = 1; event <= 2; ++event) {
        const auto result = instance->do_step_with_info(now, to_duration(1.1));
        BOOST_REQUIRE(result.result == step_result::complete);
        BOOST_REQUIRE(result.early_return);
        BOOST_REQUIRE(result.event_handling_needed);
        BOOST_REQUIRE(!result.termination_requested);
        BOOST_CHECK_SMALL(to_double_time_point(result.last_successful_time) - event, 1e-8);
        BOOST_CHECK_EQUAL(getCounter(), event);
        instance->handle_events();
        BOOST_CHECK(!instance->get_event_handling_status().termination_requested);
        BOOST_CHECK_EQUAL(getCounter(), event + 1);
        now = result.last_successful_time;
    }
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(bouncing_ball_resumes_after_state_event)
{
    const auto model = import_fmu("BouncingBall.fmu");
    const auto velocityRef = find_reference(*model, "v", variable_type::real);
    fmi::v3::co_simulation_callbacks options;
    options.event_mode_used = true;
    options.early_return_allowed = true;
    const auto instance = model->instantiate_v3_slave("ball-event", options);
    BOOST_REQUIRE(instance->event_mode_used());
    instance->setup(time_point{}, to_time_point(0.6), std::nullopt);
    instance->start_simulation();
    instance->handle_events();

    const auto first = instance->do_step_with_info(time_point{}, to_duration(0.4));
    BOOST_REQUIRE(first.result == step_result::complete);
    BOOST_REQUIRE(!first.early_return);
    BOOST_REQUIRE(!first.event_handling_needed);
    const auto impact = instance->do_step_with_info(to_time_point(0.4), to_duration(0.2));
    BOOST_REQUIRE(impact.result == step_result::complete);
    BOOST_REQUIRE(impact.early_return);
    BOOST_REQUIRE(impact.event_handling_needed);
    BOOST_CHECK_SMALL(to_double_time_point(impact.last_successful_time) - 0.453, 1e-8);

    const auto velocity = [&] {
        double value = 0.0;
        instance->get_real_variables(gsl::make_span(&velocityRef, 1), gsl::make_span(&value, 1));
        return value;
    };
    BOOST_CHECK_LT(velocity(), 0.0);
    instance->handle_events();
    BOOST_CHECK_GT(velocity(), 0.0);
    auto now = impact.last_successful_time;
    const auto stop = to_time_point(0.6);
    for (int attempt = 0; attempt < 4 && now < stop; ++attempt) {
        const auto resumed = instance->do_step_with_info(now, stop - now);
        BOOST_REQUIRE(resumed.result == step_result::complete);
        BOOST_REQUIRE(!resumed.termination_requested);
        const auto reached = resumed.early_return ? resumed.last_successful_time : stop;
        BOOST_REQUIRE(reached > now);
        BOOST_REQUIRE(reached <= stop);
        if (resumed.event_handling_needed) {
            instance->handle_events();
            BOOST_REQUIRE(!instance->get_event_handling_status().termination_requested);
        }
        now = reached;
    }
    BOOST_CHECK(now == stop);
    BOOST_CHECK_GT(velocity(), 0.0);
    instance->end_simulation();
}
