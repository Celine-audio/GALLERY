#include "helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>

#if JUCE_WINDOWS
 #include <malloc.h>
#endif

/*
    What the audio thread is and is not allowed to do.

    The rest of the suite asks whether the plugin sounds right. This one asks whether it
    is *allowed to run at all* under a real-time deadline, which is a different question
    and one that nothing else here can answer: a processBlock that allocates sounds
    perfect on a quiet machine and drops out on a busy one, and the drop-out is blamed
    on the host.

    So the allocator is replaced for the length of the block and counted. Every thread
    is counted, not just this one -- there is only one heap and a lock taken on it by a
    background thread is a lock the audio thread can wait on -- but nothing else is
    running during these cases.
*/
namespace
{
    std::atomic<int> allocations { 0 };
    std::atomic<bool> watching { false };

    void note() noexcept
    {
        if (watching.load (std::memory_order_relaxed))
            allocations.fetch_add (1, std::memory_order_relaxed);
    }

    /** Counts from here to the end of the scope, and says nothing while it is not in
        one -- the test binary allocates constantly outside the blocks under test. */
    struct Watch
    {
        Watch() { allocations.store (0); watching.store (true); }
        ~Watch() { watching.store (false); }

        int count() const { return allocations.load(); }
    };
}

//==============================================================================
namespace
{
    // Over-aligned allocation is the one part of this with no portable spelling.
    // posix_memalign does not exist on Windows, and the memory _aligned_malloc returns
    // there must go back through _aligned_free rather than free -- handing it to the
    // ordinary one is heap corruption rather than a diagnostic.
    void* alignedAllocate (std::size_t bytes, std::size_t alignment)
    {
       #if JUCE_WINDOWS
        return _aligned_malloc (bytes, alignment);
       #else
        void* p = nullptr;
        return posix_memalign (&p, alignment, bytes) == 0 ? p : nullptr;
       #endif
    }

    void alignedRelease (void* p) noexcept
    {
       #if JUCE_WINDOWS
        _aligned_free (p);
       #else
        std::free (p);
       #endif
    }
}

// Replacing these is a link-time swap, so it catches every allocation in the process --
// JUCE's, the standard library's and ours alike -- rather than only the ones written in
// this file. The aligned forms are replaced too: JUCE's SIMD types are over-aligned, and
// an unreplaced aligned new would be an allocation this test could not see.
void* operator new (std::size_t size)
{
    note();

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return ::operator new (size); }

void* operator new (std::size_t size, std::align_val_t alignment)
{
    note();

    if (auto* p = alignedAllocate (size == 0 ? 1 : size,
                                   juce::jmax (sizeof (void*), (std::size_t) alignment)))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { alignedRelease (p); }

//==============================================================================
TEST_CASE ("The allocation counter can actually fail", "[realtime]")
{
    // Every case below is a CHECK for zero, which is exactly the assertion a broken
    // counter passes. So the counter is asked to catch something first.
    //
    // Through volatile, and at a size the compiler cannot see. A plain `new int` next
    // to its own `delete` is a pair the standard lets the optimiser delete outright --
    // which it does in Release, and this case caught it: the self-check failed while
    // every real one passed, in the configuration that ships.
    static volatile std::size_t bytes = 64;

    const Watch watch;

    // Volatile, so the write and the read back are observable side effects the
    // optimiser is not allowed to remove -- and with them, the allocation behind them.
    auto* deliberate = new volatile char[bytes];
    deliberate[0] = 'x';
    const char observed = deliberate[0];
    delete[] deliberate;

    CHECK (observed == 'x');
    CHECK (watch.count() > 0);
}

TEST_CASE ("The audio thread never reaches the allocator", "[realtime]")
{
    Cabinets::FourCabinets fixture;

    juce::AudioBuffer<float> buffer (2, Cabinets::blockSize);
    juce::MidiBuffer midi;

    const auto run = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                    buffer.setSample (channel, s, 0.25f * std::sin (0.01f * (float) (i * 512 + s)));

            fixture.plugin.processBlock (buffer, midi);
        }
    };

    SECTION ("with nothing loaded")
    {
        run (8); // let every ramp settle before the allocator is watched

        const Watch watch;
        run (64);
        CHECK (watch.count() == 0);
    }

    SECTION ("with four cabinets running")
    {
        fixture.load (4);
        run (32);

        const Watch watch;
        run (64);
        CHECK (watch.count() == 0);
    }

    SECTION ("while the controls are being moved")
    {
        fixture.load (4);
        run (32);

        // Parameters are read per block, so moving them is the audio thread taking a
        // different branch rather than the message thread handing it something -- which
        // is exactly where a stray allocation would hide.
        const Watch watch;

        for (int i = 0; i < 32; ++i)
        {
            auto* align = fixture.plugin.getAPVTS().getRawParameterValue (ParamID::align[0]);
            auto* pan = fixture.plugin.getAPVTS().getRawParameterValue (ParamID::pan[1]);

            align->store (-12.0f + (float) i);
            pan->store (-1.0f + 0.06f * (float) i);

            run (1);
        }

        CHECK (watch.count() == 0);
    }

    SECTION ("while bypassed")
    {
        fixture.load (4);
        run (8);

        auto* bypass = fixture.plugin.getAPVTS().getRawParameterValue (ParamID::bypass);
        bypass->store (1.0f);

        const Watch watch;
        run (64);
        CHECK (watch.count() == 0);
    }
}

//==============================================================================
TEST_CASE ("Nothing a host can send makes the output stop being a number", "[realtime]")
{
    // Hosts do send these. A denormal-ridden tail from an upstream plugin, a NaN from a
    // misbehaving one, and full scale from somebody who has not levelled anything --
    // and a convolver is an accumulation, so one NaN in becomes every sample out and
    // stays there until the history has walked past it.
    Cabinets::FourCabinets fixture;
    fixture.load (4);

    juce::AudioBuffer<float> buffer (2, Cabinets::blockSize);
    juce::MidiBuffer midi;

    const auto feed = [&] (float value, int blocks = 4)
    {
        for (int i = 0; i < blocks; ++i)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), value,
                                                   buffer.getNumSamples());

            fixture.plugin.processBlock (buffer, midi);
        }
    };

    const auto finite = [&]
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                if (! std::isfinite (buffer.getSample (channel, s)))
                    return false;

        return true;
    };

    SECTION ("silence stays silent")
    {
        feed (0.0f);
        CHECK (finite());
    }

    SECTION ("denormals do not stall it")
    {
        feed (1.0e-38f, 8);
        CHECK (finite());
    }

    SECTION ("full scale stays finite")
    {
        feed (1.0f, 8);
        CHECK (finite());
    }

    SECTION ("it recovers from a NaN rather than staying poisoned")
    {
        feed (std::numeric_limits<float>::quiet_NaN(), 1);

        // Long enough for the convolver's history to have walked past the bad block --
        // the longest response the default tier keeps, and then some.
        feed (0.0f, 40);

        CHECK (finite());
    }
}

//==============================================================================
TEST_CASE ("A host may change the rate and the block size at any time", "[realtime]")
{
    // prepareToPlay is called again on every rate change, every buffer-size change and
    // every time the plugin is re-inserted, and a host is entitled to send a block
    // shorter than the one it prepared for. The sizes below are the awkward ones: a
    // prime, a single sample, and one either side of a power of two.
    Cabinets::FourCabinets fixture;
    fixture.load (4);

    juce::MidiBuffer midi;

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        for (const auto prepared : { 16, 64, 512, 1024, 2048 })
        {
            fixture.plugin.prepareToPlay (rate, prepared);

            for (const auto block : { 1, 7, 15, 16, 17, 63, 64, 512, 1024, 2048 })
            {
                if (block > prepared)
                    continue;

                juce::AudioBuffer<float> buffer (2, block);
                buffer.clear();

                for (int s = 0; s < block; ++s)
                    buffer.setSample (0, s, 0.5f);

                fixture.plugin.processBlock (buffer, midi);

                INFO ("rate " << rate << ", prepared " << prepared << ", block " << block);

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    for (int s = 0; s < block; ++s)
                        REQUIRE (std::isfinite (buffer.getSample (channel, s)));
            }
        }
    }
}
