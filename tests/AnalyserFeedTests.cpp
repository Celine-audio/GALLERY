/*
    The feed decides whether to rebuild the graph by comparing a signature of everything
    the picture depends on. Everything in the *audio*, that is: rebuild count, view,
    zoom, split, width, blend, and each slot's parameters. A theme change moves none of
    them, and the trace colours are handed to the displays and kept there -- so the
    cabinets went on being drawn in the hues they were last given until a knob moved.
*/
#include <PluginProcessor.h>
#include <ui/AnalyserFeed.h>
#include <ui/AnalyserGraph.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("The feed can be told to rebuild although the audio has not moved", "[ui]")
{
    PluginProcessor plugin;
    Celine::AnalyserGraph graph;
    Celine::AnalyserFeed feed { plugin, graph };

    // The first ask always answers yes: nothing has been built yet.
    REQUIRE (feed.hasChanged());

    INFO ("nothing has moved, so the picture it already has still stands");
    REQUIRE_FALSE (feed.hasChanged());

    feed.invalidate();

    INFO ("this is what a theme change has to be able to say");
    CHECK (feed.hasChanged());
}
