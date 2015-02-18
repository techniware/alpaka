/**
* \file
* Copyright 2014-2015 Benjamin Worpitz
*
* This file is part of alpaka.
*
* alpaka is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* alpaka is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with alpaka.
* If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <alpaka/traits/Mem.hpp>        // MemSpaceType

namespace alpaka
{
    namespace mem
    {
        //#############################################################################
        //! The data space on the CUDA accelerator.
        //#############################################################################
        struct MemSpaceCuda;
    }

    namespace traits
    {
        namespace mem
        {
            //#############################################################################
            //! The MemSpaceCuda memory space trait specialization.
            //#############################################################################
            template<>
            struct MemSpaceType<
                alpaka::mem::MemSpaceCuda>
            {
                using type = alpaka::mem::MemSpaceCuda;
            };
        }
    }
}