#pragma once
// Minimal runtime surface for deterministic backend locking/failure tests.
// This is not a CUDA device emulator or a substitute for GPU integration tests.
#include <cstddef>
using cudaError_t = int;
using cudaStream_t = void*;
using cudaEvent_t = void*;
constexpr unsigned cudaStreamNonBlocking = 1;
constexpr cudaError_t cudaSuccess = 0;
constexpr cudaError_t cudaErrorUnknown = 999;
enum cudaMemcpyKind { cudaMemcpyHostToDevice, cudaMemcpyDeviceToHost };
struct cudaIpcMemHandle_t { char bytes[64]; };
cudaError_t cudaGetDeviceCount(int*);
cudaError_t cudaMemGetInfo(std::size_t*, std::size_t*);
cudaError_t cudaMalloc(void**, std::size_t);
cudaError_t cudaFree(void*);
cudaError_t cudaMemcpy(void*, const void*, std::size_t, cudaMemcpyKind);
cudaError_t cudaStreamSynchronize(cudaStream_t);
cudaError_t cudaIpcGetMemHandle(cudaIpcMemHandle_t*, void*);
const char* cudaGetErrorString(cudaError_t);
cudaError_t cudaSetDevice(int);
cudaError_t cudaMallocHost(void**, std::size_t);
cudaError_t cudaFreeHost(void*);
cudaError_t cudaStreamCreateWithFlags(cudaStream_t*, unsigned);
cudaError_t cudaStreamDestroy(cudaStream_t);
cudaError_t cudaMemcpyAsync(void*, const void*, std::size_t, cudaMemcpyKind, cudaStream_t);
cudaError_t cudaEventCreate(cudaEvent_t*);
cudaError_t cudaEventDestroy(cudaEvent_t);
cudaError_t cudaEventRecord(cudaEvent_t, cudaStream_t);
cudaError_t cudaEventSynchronize(cudaEvent_t);
cudaError_t cudaEventElapsedTime(float*, cudaEvent_t, cudaEvent_t);
