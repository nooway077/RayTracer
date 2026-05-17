#pragma once

#include "stdafx.h"
#include "Helpers.h"

// Maximum number of distinct timers per frame.
static constexpr UINT kMaxTimersPerFrame = 4;

class GPUTimer
{
public:
    explicit GPUTimer(ID3D12Device* device, ID3D12CommandQueue* queue, UINT frameCount = 3)
        : m_device(device), m_queue(queue), m_frameCount(frameCount)
    {
        // Reserve queries for all in-flight frames.
        const UINT perFrameQueries = kMaxTimersPerFrame * 2;
        const UINT totalQueries = m_frameCount * perFrameQueries;

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Count = totalQueries;
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.NodeMask = 0;
        ThrowIfFailed(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap)));

        // Create one large readback buffer per frame sized for all queries.
        m_readbackBuffers.resize(m_frameCount);
        for (UINT i = 0; i < m_frameCount; ++i) {
            ThrowIfFailed(device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT64) * totalQueries),
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_readbackBuffers[i])));
        }

        // Reusable resolve resources.
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_resolveAlloc)));

        ThrowIfFailed(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_resolveAlloc.Get(), nullptr,
            IID_PPV_ARGS(&m_resolveList)));

        // CreateCommandList returns an open list, closing it so it's in a closed state initially.
        ThrowIfFailed(m_resolveList->Close());

        // Create internal fence + event for ResolveAndRead synchronization.
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_internalFence)));
        m_internalFenceValue = 1;
        m_internalFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_internalFenceEvent) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    ~GPUTimer() {
        if (m_resolveList) {
            HRESULT hr = m_resolveList->Close();
            if (FAILED(hr) && hr != DXGI_ERROR_INVALID_CALL) {
                // Ignore.
            }
        }
        if (m_internalFenceEvent) {
            CloseHandle(m_internalFenceEvent);
            m_internalFenceEvent = nullptr;
        }
    }

    // Record START timestamp in current frame's command list.
    // timerId in [0, kMaxTimersPerFrame).
    void Start(UINT timerId, UINT frameIndex, ID3D12GraphicsCommandList* cmdList) {
        if (!cmdList || frameIndex >= m_frameCount || timerId >= kMaxTimersPerFrame) return;
        const UINT slot = GetStartSlot(timerId, frameIndex);
        cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
    }

    // Record STOP timestamp.
    void Stop(UINT timerId, UINT frameIndex, ID3D12GraphicsCommandList* cmdList) {
        if (!cmdList || frameIndex >= m_frameCount || timerId >= kMaxTimersPerFrame) return;
        const UINT slot = GetStartSlot(timerId, frameIndex);
        cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot + 1);
    }

    // Resolve and read back a specific timer's duration. This method performs its own
    // submit+wait for the resolve command list before mapping, so the caller does not need
    // to wait externally.
    float ResolveAndRead(UINT timerId, UINT frameIndex) {
        if (!m_queryHeap || frameIndex >= m_frameCount || timerId >= kMaxTimersPerFrame) return 0.0;

        const UINT perFrameQueries = kMaxTimersPerFrame * 2;
        const UINT totalQueries = m_frameCount * perFrameQueries;
        const UINT startSlot = GetStartSlot(timerId, frameIndex);
        const UINT endSlot = startSlot + 1;
        const UINT64 startByte = static_cast<UINT64>(startSlot) * sizeof(UINT64);
        const UINT64 readBytes = sizeof(UINT64) * 2; // Start + End.

        // Reset and record resolve command list.
        ThrowIfFailed(m_resolveAlloc->Reset());
        ThrowIfFailed(m_resolveList->Reset(m_resolveAlloc.Get(), nullptr));

        // Resolve only the pair for this timer into the per-frame readback buffer at the correct byte offset.
        m_resolveList->ResolveQueryData(
            m_queryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            startSlot,   // Starting query index.
            2,           // Count (start + end).
            m_readbackBuffers[frameIndex].Get(),
            startByte);

        ThrowIfFailed(m_resolveList->Close());

        // Execute resolve.
        ID3D12CommandList* lists[] = { m_resolveList.Get() };
        m_queue->ExecuteCommandLists(1, lists);

        // Signal the internal fence and wait for the resolve to finish.
        const UINT64 signalValue = m_internalFenceValue++;
        ThrowIfFailed(m_queue->Signal(m_internalFence.Get(), signalValue));

        if (m_internalFence->GetCompletedValue() < signalValue) {
            ThrowIfFailed(m_internalFence->SetEventOnCompletion(signalValue, m_internalFenceEvent));
            WaitForSingleObjectEx(m_internalFenceEvent, INFINITE, FALSE);
        }

        // Read back (map only the small range).
        D3D12_RANGE readRange = { (SIZE_T)startByte, (SIZE_T)(startByte + readBytes) };
        void* mappedData = nullptr;
        HRESULT hr = m_readbackBuffers[frameIndex]->Map(0, &readRange, &mappedData);
        if (FAILED(hr) || !mappedData) return 0.0;

        // Mapped pointer to the beginning of the buffer; offset it by startByte.
        UINT8* base = static_cast<UINT8*>(mappedData);
        UINT64* timestamps = reinterpret_cast<UINT64*>(base + startByte);

        // Get timestamp frequency from the queue.
        UINT64 freq = 0;
        ThrowIfFailed(m_queue->GetTimestampFrequency(&freq));
        if (freq == 0) {
            D3D12_RANGE writtenRange = { 0, 0 };
            m_readbackBuffers[frameIndex]->Unmap(0, &writtenRange);
            return 0.0;
        }

        const UINT64 startTime = timestamps[0]; // Corresponds to startSlot.
        const UINT64 endTime = timestamps[1];   // Corresponds to endSlot.

        // Unmap.
        D3D12_RANGE writtenRange = { 0, 0 };
        m_readbackBuffers[frameIndex]->Unmap(0, &writtenRange);

        if (endTime >= startTime) {
            float ms = static_cast<float>(endTime - startTime) * 1000.0 / static_cast<float>(freq);
            return ms;
        }
        return 0.0;
    }

    UINT MaxTimersPerFrame() const { return kMaxTimersPerFrame; }

private:
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;
    UINT m_frameCount = 0;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_readbackBuffers;

    // Reusable resolve resources.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_resolveAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_resolveList;

    // Internal fence/event for waiting on resolve completion.
    Microsoft::WRL::ComPtr<ID3D12Fence> m_internalFence;
    UINT64 m_internalFenceValue = 0;
    HANDLE m_internalFenceEvent = nullptr;

    inline UINT GetStartSlot(UINT timerId, UINT frameIndex) const {
        const UINT perFrame = kMaxTimersPerFrame * 2;
        return frameIndex * perFrame + timerId * 2;
    }
};
