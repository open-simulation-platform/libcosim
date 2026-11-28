/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/utility/uuid.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <string>


namespace cosim::utility
{


std::string random_uuid() noexcept // NOLINT(bugprone-exception-escape): Preserve the existing noexcept contract.
{
    boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}


} // namespace cosim::utility
