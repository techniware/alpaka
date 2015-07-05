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

// Specialized traits.
#include <alpaka/acc/Traits.hpp>            // AccType
#include <alpaka/dev/Traits.hpp>            // DevType
#include <alpaka/event/Traits.hpp>          // EventType
#include <alpaka/exec/Traits.hpp>           // ExecType
#include <alpaka/size/Traits.hpp>           // size::SizeType
#include <alpaka/stream/Traits.hpp>         // StreamType

// Implementation details.
#include <alpaka/acc/cuda/Acc.hpp>          // AccGpuCuda
#include <alpaka/dev/DevCudaRt.hpp>         // DevCudaRt
#include <alpaka/event/EventCudaRt.hpp>     // EventCudaRt
#include <alpaka/kernel/Traits.hpp>         // BlockSharedExternMemSizeBytes
#include <alpaka/stream/StreamCudaRt.hpp>   // StreamCudaRt

#include <alpaka/core/Cuda.hpp>             // ALPAKA_CUDA_RT_CHECK

#include <boost/predef.h>                   // workarounds

#include <stdexcept>                        // std::runtime_error
#if ALPAKA_DEBUG >= ALPAKA_DEBUG_MINIMAL
    #include <iostream>                     // std::cout
#endif

namespace alpaka
{
    namespace exec
    {
        namespace cuda
        {
            namespace detail
            {
                //-----------------------------------------------------------------------------
                //! The GPU CUDA kernel entry point.
                // \NOTE: A __global__ function or function template cannot have a trailing return type.
                //-----------------------------------------------------------------------------
                template<
                    typename TDim,
                    typename TSize,
                    typename TKernelFnObj,
                    typename... TArgs>
                __global__ void cudaKernel(
                    TKernelFnObj kernelFnObj,
                    TArgs ... args)
                {
#if defined(__CUDA_ARCH__) && (__CUDA_ARCH__ < 200)
    #error "Cuda device capability >= 2.0 is required!"
#endif
                    acc::cuda::detail::AccGpuCuda<TDim, TSize> acc;

                    kernelFnObj(
                        const_cast<acc::cuda::detail::AccGpuCuda<TDim, TSize> const &>(acc),
                        args...);
                }
            }
        }

        //#############################################################################
        //! The GPU CUDA accelerator executor.
        //#############################################################################
        template<
            typename TDim,
            typename TSize>
        class ExecGpuCuda final :
            public workdiv::WorkDivMembers<TDim, TSize>
        {
        public:
            //-----------------------------------------------------------------------------
            //! Constructor.
            //-----------------------------------------------------------------------------
            template<
                typename TWorkDiv>
            ALPAKA_FN_HOST ExecGpuCuda(
                TWorkDiv const & workDiv,
                stream::StreamCudaRt & stream) :
                    workdiv::WorkDivMembers<TDim, TSize>(workDiv),
                    m_Stream(stream)
            {
                ALPAKA_DEBUG_MINIMAL_LOG_SCOPE;
            }
            //-----------------------------------------------------------------------------
            //! Copy constructor.
            //-----------------------------------------------------------------------------
            ALPAKA_FN_HOST ExecGpuCuda(ExecGpuCuda const &) = default;
            //-----------------------------------------------------------------------------
            //! Move constructor.
            //-----------------------------------------------------------------------------
            ALPAKA_FN_HOST ExecGpuCuda(ExecGpuCuda &&) = default;
            //-----------------------------------------------------------------------------
            //! Copy assignment operator.
            //-----------------------------------------------------------------------------
            ALPAKA_FN_HOST auto operator=(ExecGpuCuda const &) -> ExecGpuCuda & = default;
            //-----------------------------------------------------------------------------
            //! Move assignment operator.
            //-----------------------------------------------------------------------------
            ALPAKA_FN_HOST auto operator=(ExecGpuCuda &&) -> ExecGpuCuda & = default;
            //-----------------------------------------------------------------------------
            //! Destructor.
            //-----------------------------------------------------------------------------
            ALPAKA_FN_HOST ~ExecGpuCuda() = default;

            //-----------------------------------------------------------------------------
            //! Executes the kernel function object.
            //-----------------------------------------------------------------------------
            template<
                typename TKernelFnObj,
                typename... TArgs>
            ALPAKA_FN_HOST auto operator()(
                // \NOTE: No const reference (const &) is allowed as the parameter type because the kernel launch language extension expects the arguments by value.
                // This forces the type of a float argument given with std::forward to this function to be of type float instead of e.g. "float const & __ptr64" (MSVC).
                // If not given by value, the kernel launch code does not copy the value but the pointer to the value location.
                TKernelFnObj kernelFnObj,
                TArgs ... args) const
            -> void
            {
                ALPAKA_DEBUG_MINIMAL_LOG_SCOPE;

#if (!__GLIBCXX__) // libstdc++ even for gcc-4.9 does not support std::is_trivially_copyable.
                static_assert(
                    std::is_trivially_copyable<TKernelFnObj>::value,
                    "The given kernel function object has to fulfill is_trivially_copyable!");
#endif
                // TODO: Check that (sizeof(TKernelFnObj) * m_3uiBlockThreadExtents.prod()) < available memory size

#if ALPAKA_DEBUG >= ALPAKA_DEBUG_FULL
                //std::size_t uiPrintfFifoSize;
                //cudaDeviceGetLimit(&uiPrintfFifoSize, cudaLimitPrintfFifoSize);
                //std::cout << BOOST_CURRENT_FUNCTION << "INFO: uiPrintfFifoSize: " << uiPrintfFifoSize << std::endl;
                //cudaDeviceSetLimit(cudaLimitPrintfFifoSize, uiPrintfFifoSize*10);
                //cudaDeviceGetLimit(&uiPrintfFifoSize, cudaLimitPrintfFifoSize);
                //std::cout << BOOST_CURRENT_FUNCTION << "INFO: uiPrintfFifoSize: " <<  uiPrintfFifoSize << std::endl;
#endif

                auto const vuiGridBlockExtents(
                    workdiv::getWorkDiv<Grid, Blocks>(
                        *static_cast<workdiv::WorkDivMembers<TDim, TSize> const *>(this)));
                auto const vuiBlockThreadExtents(
                    workdiv::getWorkDiv<Block, Threads>(
                        *static_cast<workdiv::WorkDivMembers<TDim, TSize> const *>(this)));

                dim3 gridDim(1u, 1u, 1u);
                dim3 blockDim(1u, 1u, 1u);
                // \FIXME: CUDA currently supports a maximum of 3 dimensions!
                for(auto i(static_cast<typename TDim::value_type>(0)); i<std::min(static_cast<typename TDim::value_type>(3), TDim::value); ++i)
                {
                    reinterpret_cast<unsigned int *>(&gridDim)[i] = vuiGridBlockExtents[TDim::value-1u-i];
                    reinterpret_cast<unsigned int *>(&blockDim)[i] = vuiBlockThreadExtents[TDim::value-1u-i];
                }
                // Assert that all extents of the higher dimensions are 1!
                for(auto i(std::min(static_cast<typename TDim::value_type>(3), TDim::value)); i<TDim::value; ++i)
                {
                    assert(vuiGridBlockExtents[TDim::value-1u-i] == 1);
                    assert(vuiBlockThreadExtents[TDim::value-1u-i] == 1);
                }

#if ALPAKA_DEBUG >= ALPAKA_DEBUG_FULL
                std::cout << BOOST_CURRENT_FUNCTION << "gridDim: " <<  gridDim.z << " " <<  gridDim.y << " " <<  gridDim.x << std::endl;
                std::cout << BOOST_CURRENT_FUNCTION << "blockDim: " <<  blockDim.z << " " <<  blockDim.y << " " <<  blockDim.x << std::endl;
#endif

                // Get the size of the block shared extern memory.
                auto const uiBlockSharedExternMemSizeBytes(
                    kernel::getBlockSharedExternMemSizeBytes<
                        typename std::decay<TKernelFnObj>::type,
                        AccGpuCuda<TDim, TSize>>(
                            vuiBlockThreadExtents,
                            args...));
#if ALPAKA_DEBUG >= ALPAKA_DEBUG_FULL
                // Log the block shared memory size.
                std::cout << BOOST_CURRENT_FUNCTION
                    << " BlockSharedExternMemSizeBytes: " << uiBlockSharedExternMemSizeBytes << " B"
                    << std::endl;
#endif

#if ALPAKA_DEBUG >= ALPAKA_DEBUG_FULL
                // Log the function attributes.
                cudaFuncAttributes funcAttrs;
                cudaFuncGetAttributes(&funcAttrs, cuda::detail::cudaKernel<TDim, TSize, TKernelFnObj, TArgs...>);
                std::cout << BOOST_CURRENT_FUNCTION
                    << " binaryVersion: " << funcAttrs.binaryVersion
                    << " constSizeBytes: " << funcAttrs.constSizeBytes << " B"
                    << " localSizeBytes: " << funcAttrs.localSizeBytes << " B"
                    << " maxThreadsPerBlock: " << funcAttrs.maxThreadsPerBlock
                    << " numRegs: " << funcAttrs.numRegs
                    << " ptxVersion: " << funcAttrs.ptxVersion
                    << " sharedSizeBytes: " << funcAttrs.sharedSizeBytes << " B"
                    << std::endl;
#endif

                // Set the current device.
                ALPAKA_CUDA_RT_CHECK(cudaSetDevice(
                    m_Stream.m_spStreamCudaImpl->m_Dev.m_iDevice));
                // Enqueue the kernel execution.
                cuda::detail::cudaKernel<TDim, TSize, TKernelFnObj, TArgs...><<<
                    gridDim,
                    blockDim,
                    uiBlockSharedExternMemSizeBytes,
                    m_Stream.m_spStreamCudaImpl->m_CudaStream>>>(
                        kernelFnObj,
                        args...);

#if ALPAKA_DEBUG >= ALPAKA_DEBUG_MINIMAL
                // Wait for the kernel execution to finish but do not check error return of this call.
                // Do not use the alpaka::wait method because it checks the error itself but we want to give a custom error message.
                cudaStreamSynchronize(m_Stream.m_spStreamCudaImpl->m_CudaStream);
                //cudaDeviceSynchronize();
                cudaError_t const error(cudaGetLastError());
                if(error != cudaSuccess)
                {
                    std::string const sError("The execution of kernel '" + std::string(typeid(TKernelFnObj).name()) + "' failed with error: '" + std::string(cudaGetErrorString(error)) + "'");
                    std::cerr << sError << std::endl;
                    ALPAKA_DEBUG_BREAK;
                    throw std::runtime_error(sError);
                }
#endif
            }

        public:
            stream::StreamCudaRt m_Stream;
        };
    }

    namespace acc
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor accelerator type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct AccType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = acc::cuda::detail::AccGpuCuda<TDim, TSize>;
            };
        }
    }
    namespace dev
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor device type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct DevType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = dev::DevCudaRt;
            };
            //#############################################################################
            //! The GPU CUDA executor device manager type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct DevManType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = dev::DevManCudaRt;
            };
        }
    }
    namespace dim
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor dimension getter trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct DimType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = TDim;
            };
        }
    }
    namespace event
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor event type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct EventType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = event::EventCudaRt;
            };
        }
    }
    namespace exec
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor executor type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct ExecType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = exec::ExecGpuCuda<TDim, TSize>;
            };
        }
    }
    namespace size
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor size type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct SizeType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = TSize;
            };
        }
    }
    namespace stream
    {
        namespace traits
        {
            //#############################################################################
            //! The GPU CUDA executor stream type trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct StreamType<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                using type = stream::StreamCudaRt;
            };
            //#############################################################################
            //! The GPU CUDA executor stream get trait specialization.
            //#############################################################################
            template<
                typename TDim,
                typename TSize>
            struct GetStream<
                exec::ExecGpuCuda<TDim, TSize>>
            {
                ALPAKA_FN_HOST static auto getStream(
                    exec::ExecGpuCuda<TDim, TSize> const & exec)
                -> stream::StreamCudaRt
                {
                    return exec.m_Stream;
                }
            };
        }
    }
}