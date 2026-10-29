#include "RoutingSelector.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/LookAndFeelBase.h>
#include <CelineUI/Theme.h>

using namespace Celine;

namespace
{
    constexpr int gap = 8;

    /** The library header's buttons, so the two headers in one column match. */
    constexpr int buttonHeight = 28;

    /** Wide enough for a line of four dots to read as four, rather than as a dashed
        line, at the size the window opens at. */
    constexpr int buttonWidth = 46;

    const char* describe (Parameters::Routing routing)
    {
        switch (routing)
        {
            case Parameters::Routing::twoChains:
                return "Two chains: 1 into 2, beside 3 into 4. The pad blends the two, top to bottom.";

            case Parameters::Routing::series:
                return "Series: 1 into 2 into 3 into 4. One path, so nothing to blend.";

            case Parameters::Routing::parallel:
                break;
        }

        return "Parallel: each cabinet hears the input, and the pad blends all four.";
    }
}

//==============================================================================
class RoutingSelector::Choice : public juce::Button
{
public:
    explicit Choice (Parameters::Routing routingToDraw)
        : juce::Button (Parameters::routingNames[(size_t) routingToDraw]), routing (routingToDraw)
    {
        setTooltip (describe (routing));
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);
        const auto on = getToggleState();

        // Lit as the graph's front tab is lit -- the accent at a fifth -- rather than in
        // the accent itself: four cabinet colours on a full purple ground lose the
        // purple one, which is the one thing a picture made of colours cannot afford.
        //
        // Unlit, the house button: these stand in a header beside the library's, whose
        // buttons wear it.
        auto fill = on ? Theme::tabActive() : Theme::button();

        g.setColour (Theme::underPointer (fill, highlighted, down));
        g.fillRoundedRectangle (bounds, Theme::cornerRadius);

        if (on)
        {
            g.setColour (Theme::accent());
            g.drawRoundedRectangle (bounds.reduced (0.5f), Theme::cornerRadius, Theme::borderWidth);
        }

        // Faded rather than greyed when not chosen: the colours are what tell the three
        // apart, so they stay -- only quieter than the one in force.
        drawWiring (g, routing, bounds.reduced (8.0f, 5.0f), on || highlighted ? 1.0f : 0.55f);
    }

    const Parameters::Routing routing;
};

//==============================================================================
RoutingSelector::RoutingSelector (juce::RangedAudioParameter& routingParameter)
{
    title.setText ("ROUTING", juce::dontSendNotification);

    // The library's title face, letter for letter, for the same reason it is set
    // there: this sits in the same column as that header, and the two are one family.
    title.getProperties().set (keepFontProperty, true);
    title.setFont (Fonts::logo (juce::jmin (24.0f, (float) Theme::tabBarHeight * 0.36f)));
    title.setJustificationType (juce::Justification::centredLeft);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    for (size_t index = 0; index < choices.size(); ++index)
    {
        const auto routing = (Parameters::Routing) index;
        auto choice = std::make_unique<Choice> (routing);

        choice->onClick = [this, routing]
        {
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture ((float) routing);
        };

        addAndMakeVisible (*choice);
        choices[index] = std::move (choice);
    }

    attachment = std::make_unique<juce::ParameterAttachment> (
        routingParameter,
        [this] (float value) { select (Parameters::routingForChoice (juce::roundToInt (value))); });

    attachment->sendInitialUpdate();

    lookAndFeelChanged();
}

void RoutingSelector::lookAndFeelChanged()
{
    title.setColour (juce::Label::textColourId, Theme::text());
}

RoutingSelector::~RoutingSelector() = default;

void RoutingSelector::select (Parameters::Routing routing)
{
    current = routing;

    for (auto& choice : choices)
        choice->setToggleState (choice->routing == routing, juce::dontSendNotification);

    if (onChanged != nullptr)
        onChanged (routing);
}

//==============================================================================
void RoutingSelector::drawWiring (juce::Graphics& g, Parameters::Routing routing,
                                  juce::Rectangle<float> area, float alpha)
{
    // One line per chain, and a dot on it per cabinet, left to right in the order the
    // signal reaches them. Rows are what is in parallel; along a row is what is in
    // series. That is the whole grammar, and all three pictures are written in it.
    const auto chains = routing == Parameters::Routing::series    ? 1
                      : routing == Parameters::Routing::twoChains ? 2
                                                                  : ParamID::numSlots;
    const auto perChain = ParamID::numSlots / chains;

    const auto rowHeight = area.getHeight() / (float) chains;
    const auto spacing = area.getWidth() / (float) perChain;

    // As big as the tightest direction allows and no bigger: four dots along a line
    // that touch read as one bar, and four lines whose dots touch read as one column.
    const auto radius = juce::jmin (3.0f, rowHeight * 0.42f, spacing * 0.3f);
    const auto stroke = 1.0f;

    for (int chain = 0; chain < chains; ++chain)
    {
        const auto y = area.getY() + rowHeight * ((float) chain + 0.5f);

        g.setColour (Theme::textDim().withMultipliedAlpha (alpha * 0.8f));
        g.drawLine (area.getX(), y, area.getRight(), y, stroke);

        for (int step = 0; step < perChain; ++step)
        {
            const auto slot = chain * perChain + step;
            const auto x = area.getX() + area.getWidth() * ((float) step + 0.5f) / (float) perChain;

            g.setColour (Theme::irSlot (slot).withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre ({ x, y }));
        }
    }
}

//==============================================================================
void RoutingSelector::paint (juce::Graphics& g)
{
    // The band the library's title sits in, rounded at the top only: the pad hangs off
    // the bottom of it, and the two are one panel.
    const auto full = getLocalBounds().toFloat();

    juce::Path band;
    band.addRoundedRectangle (full.getX(), full.getY(), full.getWidth(), full.getHeight(),
                              Theme::cornerRadius, Theme::cornerRadius, true, true, false, false);

    g.setColour (Theme::tabInactive());
    g.fillPath (band);
}

void RoutingSelector::resized()
{
    auto header = getLocalBounds().reduced (gap + 2, 0);
    auto row = header.withSizeKeepingCentre (header.getWidth(), buttonHeight);

    // Right to left, so the three keep their pitch whatever the column's width, and the
    // title takes what is left -- measured, as the library's is, because the display
    // face is a good deal wider than the body one.
    for (auto index = (int) choices.size() - 1; index >= 0; --index)
    {
        choices[(size_t) index]->setBounds (row.removeFromRight (buttonWidth));

        if (index > 0)
            row.removeFromRight (gap - 3);
    }

    const auto needed = juce::GlyphArrangement::getStringWidthInt (title.getFont(), title.getText()) + 6;
    title.setBounds (header.removeFromLeft (juce::jmin (needed, row.getWidth())));
}
