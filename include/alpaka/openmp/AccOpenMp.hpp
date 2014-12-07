/**
* Copyright 2014 Benjamin Worpitz
*
* This file is part of alpaka.
*
* alpaka is free software: you can redistribute it and/or modify
* it under the terms of either the GNU General Public License or
* the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* alpaka is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License and the GNU Lesser General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License
* and the GNU Lesser General Public License along with alpaka.
* If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

// base classes

#include <alpaka/openmp/AccOpenMpFwd.hpp>
#include <alpaka/openmp/WorkSize.hpp>               // TInterfacedWorkSize
#include <alpaka/openmp/Index.hpp>                  // TInterfacedIndex
#include <alpaka/openmp/Atomic.hpp>                 // TInterfacedAtomic

// user functionality
#include <alpaka/host/Memory.hpp>                   // MemCopy
#include <alpaka/openmp/Event.hpp>                  // Event
#include <alpaka/openmp/Device.hpp>                 // Devices

// specialized templates
#include <alpaka/interfaces/KernelExecCreator.hpp>  // KernelExecCreator

// implementation details
#include <alpaka/openmp/Common.hpp>
#include <alpaka/interfaces/BlockSharedExternMemSizeBytes.hpp>
#include <alpaka/interfaces/IAcc.hpp>

#include <cstddef>                                  // std::size_t
#include <cstdint>                                  // std::uint32_t
#include <vector>                                   // std::vector
#include <cassert>                                  // assert
#include <stdexcept>                                // std::except
#include <string>                                   // std::to_string
#include <algorithm>                                // std::min, std::max
#ifdef ALPAKA_DEBUG
    #include <iostream>                             // std::cout
#endif

#include <boost/mpl/apply.hpp>                      // boost::mpl::apply

namespace alpaka
{
    namespace openmp
    {
        namespace detail
        {
            template<typename TAcceleratedKernel>
            class KernelExecutor;

            //#############################################################################
            //! The base class for all OpenMP accelerated kernels.
            //#############################################################################
            class AccOpenMp :
                protected TInterfacedWorkSize,
                protected TInterfacedIndex,
                protected TInterfacedAtomic
            {
            public:
                using MemorySpace = MemorySpaceHost;

                template<typename TAcceleratedKernel>
                friend class alpaka::openmp::detail::KernelExecutor;

            public:
                //-----------------------------------------------------------------------------
                //! Constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AccOpenMp() :
                    TInterfacedWorkSize(),
                    TInterfacedIndex(*static_cast<TInterfacedWorkSize const *>(this), m_v3uiGridBlockIdx),
                    TInterfacedAtomic()
                {}
                //-----------------------------------------------------------------------------
                //! Copy-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AccOpenMp(AccOpenMp const &) = default;
                //-----------------------------------------------------------------------------
                //! Move-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AccOpenMp(AccOpenMp &&) = default;
                //-----------------------------------------------------------------------------
                //! Assignment-operator.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST AccOpenMp & operator=(AccOpenMp const &) = delete;
                //-----------------------------------------------------------------------------
                //! Destructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST ~AccOpenMp() noexcept = default;

            protected:
                //-----------------------------------------------------------------------------
                //! \return The requested index.
                //-----------------------------------------------------------------------------
                template<typename TOrigin, typename TUnit, typename TDimensionality = dim::D3>
                ALPAKA_FCT_HOST typename alpaka::detail::DimToRetType<TDimensionality>::type getIdx() const
                {
                    return this->TInterfacedIndex::getIdx<TOrigin, TUnit, TDimensionality>(
                        *static_cast<TInterfacedWorkSize const *>(this));
                }

                //-----------------------------------------------------------------------------
                //! Syncs all kernels in the current block.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST void syncBlockKernels() const
                {
                    #pragma omp barrier
                }

                //-----------------------------------------------------------------------------
                //! \return Allocates block shared memory.
                //-----------------------------------------------------------------------------
                template<typename T, std::size_t TuiNumElements>
                ALPAKA_FCT_HOST T * allocBlockSharedMem() const
                {
                    static_assert(TuiNumElements > 0, "The number of elements to allocate in block shared memory must not be zero!");

                    // Assure that all threads have executed the return of the last allocBlockSharedMem function (if there was one before).
                    syncBlockKernels();

                    // Arbitrary decision: The thread with id 0 has to allocate the memory.
                    if(::omp_get_thread_num() == 0)
                    {
                        // TODO: Optimize: do not initialize the memory on allocation like std::vector does!
                        m_vvuiSharedMem.emplace_back(TuiNumElements);
                    }
                    syncBlockKernels();

                    return reinterpret_cast<T*>(m_vvuiSharedMem.back().data());
                }

                //-----------------------------------------------------------------------------
                //! \return The pointer to the externally allocated block shared memory.
                //-----------------------------------------------------------------------------
                template<typename T>
                ALPAKA_FCT_HOST T * getBlockSharedExternMem() const
                {
                    return reinterpret_cast<T*>(m_vuiExternalSharedMem.data());
                }

#ifdef ALPAKA_NVCC_FRIEND_ACCESS_BUG
            protected:
#else
            private:
#endif
                // getIdx
                vec<3u> mutable m_v3uiGridBlockIdx;                          //!< The index of the currently executed block.

                // allocBlockSharedMem
                std::vector<std::vector<uint8_t>> mutable m_vvuiSharedMem;  //!< Block shared memory.

                // getBlockSharedExternMem
                std::vector<uint8_t> mutable m_vuiExternalSharedMem;        //!< External block shared memory.
            }; 

            //#############################################################################
            //! The executor for an OpenMP accelerated kernel.
            //#############################################################################
            template<typename TAcceleratedKernel>
            class KernelExecutor :
                private TAcceleratedKernel
            {
                static_assert(std::is_base_of<IAcc<AccOpenMp>, TAcceleratedKernel>::value, "The TAcceleratedKernel for the openmp::detail::KernelExecutor has to inherit from IAcc<AccOpenMp>!");

            public:
                using TAcc = AccOpenMp;

            public:
                //-----------------------------------------------------------------------------
                //! Constructor.
                //-----------------------------------------------------------------------------
                template<typename TWorkSize, typename... TKernelConstrArgs>
                ALPAKA_FCT_HOST KernelExecutor(IWorkSize<TWorkSize> const & workSize, TKernelConstrArgs && ... args) :
                    TAcceleratedKernel(std::forward<TKernelConstrArgs>(args)...)
                {
#ifdef ALPAKA_DEBUG
                    std::cout << "[+] AccOpenMp::KernelExecutor()" << std::endl;
#endif
                    (*const_cast<TInterfacedWorkSize*>(static_cast<TInterfacedWorkSize const *>(this))) = workSize;

                    /*auto const uiNumKernelsPerBlock(workSize.template getSize<Block, Kernels, Linear>());
                    auto const uiMaxKernelsPerBlock(AccOpenMp::getSizeBlockKernelsLinearMax());
                    if(uiNumKernelsPerBlock > uiMaxKernelsPerBlock)
                    {
                        throw std::runtime_error(("The given blockSize '" + std::to_string(uiNumKernelsPerBlock) + "' is larger then the supported maximum of '" + std::to_string(uiMaxKernelsPerBlock) + "' by the OpenMp accelerator!").c_str());
                    }*/

                    m_v3uiSizeGridBlocks = workSize.template getSize<Grid, Blocks, D3>();
                    m_v3uiSizeBlockKernels = workSize.template getSize<Block, Kernels, D3>();
#ifdef ALPAKA_DEBUG
                    std::cout << "[-] AccOpenMp::KernelExecutor()" << std::endl;
#endif
                }
                //-----------------------------------------------------------------------------
                //! Copy-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST KernelExecutor(KernelExecutor const &) = default;
                //-----------------------------------------------------------------------------
                //! Move-constructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST KernelExecutor(KernelExecutor &&) = default;
                //-----------------------------------------------------------------------------
                //! Assignment-operator.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST KernelExecutor & operator=(KernelExecutor const &) = delete;
                //-----------------------------------------------------------------------------
                //! Destructor.
                //-----------------------------------------------------------------------------
                ALPAKA_FCT_HOST ~KernelExecutor() noexcept = default;

                //-----------------------------------------------------------------------------
                //! Executes the accelerated kernel.
                //-----------------------------------------------------------------------------
                template<typename... TArgs>
                ALPAKA_FCT_HOST void operator()(TArgs && ... args) const
                {
#ifdef ALPAKA_DEBUG
                    std::cout << "[+] AccOpenMp::KernelExecutor::operator()" << std::endl;
#endif
                    auto const uiBlockSharedExternMemSizeBytes(BlockSharedExternMemSizeBytes<TAcceleratedKernel>::getBlockSharedExternMemSizeBytes(m_v3uiSizeBlockKernels, std::forward<TArgs>(args)...));
                    this->AccOpenMp::m_vuiExternalSharedMem.resize(uiBlockSharedExternMemSizeBytes);
#ifdef ALPAKA_DEBUG
                    //std::cout << "GridBlocks: " << v3uiSizeGridBlocks << " BlockKernels: " << v3uiSizeBlockKernels << std::endl;
#endif
                    // CUDA programming guide: "Thread blocks are required to execute independently: It must be possible to execute them in any order, in parallel or in series. 
                    // This independence requirement allows thread blocks to be scheduled in any order across any number of cores"
                    // -> We can execute them serially.
                    for(std::uint32_t bz(0); bz<m_v3uiSizeGridBlocks[2]; ++bz)
                    {
                        this->AccOpenMp::m_v3uiGridBlockIdx[2] = bz;
                        for(std::uint32_t by(0); by<m_v3uiSizeGridBlocks[1]; ++by)
                        {
                            this->AccOpenMp::m_v3uiGridBlockIdx[1] = by;
                            for(std::uint32_t bx(0); bx<m_v3uiSizeGridBlocks[0]; ++bx)
                            {
                                this->AccOpenMp::m_v3uiGridBlockIdx[0] = bx;

                                // The number of threads in this block.
                                auto const uiNumKernelsInBlock(this->AccOpenMp::getSize<Block, Kernels, Linear>());

                                // Force the environment to use the given number of threads.
                                ::omp_set_dynamic(0);

                                // Parallelizing the threads is required because when syncBlockKernels is called all of them have to be done with their work up to this line.
                                // So we have to spawn one real thread per thread in a block.
                                // 'omp for' is not useful because it is meant for cases where multiple iterations are executed by one thread but in our case a 1:1 mapping is required.
                                // Therefore we use 'omp parallel' with the specified number of threads in a block.
                                //
                                // TODO: Does this hinder executing multiple kernels in parallel because their block sizes/omp thread numbers are interfering? Is this a real use case? 
                                #pragma omp parallel num_threads(static_cast<int>(uiNumKernelsInBlock))
                                {
#ifdef ALPAKA_DEBUG
                                    if((::omp_get_thread_num() == 0) && (bz == 0) && (by == 0) && (bx == 0))
                                    {
                                        assert(::omp_get_num_threads()>=0);
                                        auto const uiNumThreads(static_cast<std::uint32_t>(::omp_get_num_threads()));
                                        std::cout << "omp_get_num_threads: " << uiNumThreads << std::endl;
                                        if(uiNumThreads != uiNumKernelsInBlock)
                                        {
                                            throw std::runtime_error("The OpenMP runtime did not use the number of threads that had been required!");
                                        }
                                    }
#endif
                                    this->TAcceleratedKernel::operator()(std::forward<TArgs>(args)...);

                                    // Wait for all threads to finish before deleting the shared memory.
                                    this->AccOpenMp::syncBlockKernels();
                                }
                            }
                        }
                    }

                    // After all blocks have been processed, the shared memory can be deleted.
                    this->AccOpenMp::m_vvuiSharedMem.clear();
                    this->AccOpenMp::m_vuiExternalSharedMem.clear();
#ifdef ALPAKA_DEBUG
                    std::cout << "[-] AccOpenMp::KernelExecutor::operator()" << std::endl;
#endif
                }

            private:
                vec<3u> m_v3uiSizeGridBlocks;
                vec<3u> m_v3uiSizeBlockKernels;
            };
        }
    }

    namespace detail
    {
        //#############################################################################
        //! The serial kernel executor builder.
        //#############################################################################
        template<typename TKernel, typename... TKernelConstrArgs>
        class KernelExecCreator<AccOpenMp, TKernel, TKernelConstrArgs...>
        {
        public:
            using TAcceleratedKernel = typename boost::mpl::apply<TKernel, AccOpenMp>::type;
            using KernelExecutorExtent = KernelExecutorExtent<openmp::detail::KernelExecutor<TAcceleratedKernel>, TKernelConstrArgs...>;

            //-----------------------------------------------------------------------------
            //! Creates an kernel executor for the serial accelerator.
            //-----------------------------------------------------------------------------
            ALPAKA_FCT_HOST KernelExecutorExtent operator()(TKernelConstrArgs && ... args) const
            {
                return KernelExecutorExtent(std::forward<TKernelConstrArgs>(args)...);
            }
        };
    }
}