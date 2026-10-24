#include "PluginEditor.h"
#include "ui/OniriqueWindowSize.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    OniriqueLookAndFeel& sharedLookAndFeel()
    {
        static OniriqueLookAndFeel laf;
        return laf;
    }

    constexpr int kHeaderHeight = 68;
    constexpr int kTitleLeft = 72;
    constexpr int kFootHeight = 46;
    constexpr int kPanelGap = 12;
    constexpr int kPanelHeading = 26;

    // How many controls wide each family is.
    constexpr int kPanelColumns[] = { 4, 2, 3 };
    constexpr int kColumns = 9;
    const char* const kPanelNames[] = { "ECHO", "CHARACTER", "COLOUR" };

    Font titleFont (bool heavy)
    {
        return displayFont (23.0f, heavy).withExtraKerningFactor (0.09f);
    }

    /** A four-pointed star, as on the Onirique sigil. */
    Path star (Point<float> centre, float radius)
    {
        Path p;
        const auto waist = radius * 0.16f;

        p.startNewSubPath (centre.x, centre.y - radius);
        p.lineTo (centre.x + waist, centre.y - waist);
        p.lineTo (centre.x + radius, centre.y);
        p.lineTo (centre.x + waist, centre.y + waist);
        p.lineTo (centre.x, centre.y + radius);
        p.lineTo (centre.x - waist, centre.y + waist);
        p.lineTo (centre.x - radius, centre.y);
        p.lineTo (centre.x - waist, centre.y - waist);
        p.closeSubPath();
        return p;
    }

    constexpr int kLogoWidth = 44;
    constexpr int kLogoMargin = 8;   // room for the glow around it

    Image makeLogo()
    {
        const auto sigil = ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        if (! sigil.isValid())
            return {};

        const auto width = 2 * kLogoWidth;
        const auto height = width * sigil.getHeight() / sigil.getWidth();
        const auto margin = 2 * kLogoMargin;

        Image padded (Image::ARGB, width + 2 * margin, height + 2 * margin, true);

        {
            Graphics g (padded);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<int> (margin, margin, width, height).toFloat());
        }

        Image result (Image::ARGB, padded.getWidth(), padded.getHeight(), true);
        Graphics g (result);

        GlowEffect glow;
        glow.setGlowProperties (7.0f, ember.withAlpha (0.8f));
        glow.applyEffect (padded, g, 1.0f, 1.0f);

        return result;
    }
}

EchoBoxEditor::EchoBoxEditor (EchoBoxProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      display (p),
      browser (p)
{
    setLookAndFeel (&sharedLookAndFeel());
    logo = makeLogo();

    auto& state = processor.apvts;

    addAndMakeVisible (display);

    // Presets.
    for (auto* b : { &previousButton, &nextButton, &saveButton, &deleteButton, &slotAButton, &slotBButton, &copyButton })
    {
        b->getProperties().set ("plain", true);
        addAndMakeVisible (*b);
    }

    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { beginSaving(); };
    deleteButton.onClick = [this] { deleteClicked(); };

    // The preset's name is what opens the browser, where one is chosen.
    presetBox.onOpen = [this] { browser.open(); };
    addAndMakeVisible (presetBox);

    browser.onClose = [this] { refreshPresets(); };
    addChildComponent (browser);

    // The name of a preset being saved is typed where its name is shown.
    // It lies over the menu and looks like it, with the text where the menu
    // had it, so that the name seems to be typed into the menu itself.
    presetNameEditor.getProperties().set ("box", true);
    presetNameEditor.setJustificationType (Justification::centredLeft);
    presetNameEditor.setBorderSize ({ 0, 13, 0, 30 });
    presetNameEditor.setEditable (true);
    presetNameEditor.onTextChange = [this]
    {
        const auto saved = processor.saveUserPreset (presetNameEditor.getText());

        if (saved.isNotEmpty())
            refreshPresets();
    };
    presetNameEditor.onEditorHide = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);

    refreshPresets();

    // A and B.
    slotAButton.onClick = [this] { processor.switchToSlot (0); };
    slotBButton.onClick = [this] { processor.switchToSlot (1); };
    copyButton.onClick = [this] { processor.copyToOtherSlot(); };

    const std::pair<Knob*, const char*> knobs[] = { { &feedback, "feedback" }, { &mix, "mix" }, { &delayOut, "delayout" }, { &character, "character" },
                                                    { &tone, "tone" }, { &pitch, "pitch" }, { &wobble, "wobble" },
                                                    { &pingPong, "pingpong" }, { &space, "space" } };

    for (const auto& [knob, id] : knobs)
    {
        knob->attach (state, id);
        addAndMakeVisible (*knob);
    }

    addAndMakeVisible (time);
    attachTime (state.getRawParameterValue ("sync")->load() > 0.5f);

    character.setEnds ("DIG", "TAPE");
    tone.setEnds ("DARK", "BRIGHT");
    pitch.setEnds ("-12", "+12");
    feedback.setEnds ("0", "110");

    for (auto* b : { &syncButton, &freezeButton, &bypassButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }

    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "sync", syncButton));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "freeze", freezeButton));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "bypass", bypassButton));

    // The three heads, each switched on and off by itself; one is always on.
    headsAttachment = std::make_unique<ParameterAttachment> (*state.getParameter ("heads"), [this] (float value)
    {
        const auto mask = ebx::headMask (roundToInt (value));

        for (int k = 0; k < ebx::kNumHeads; ++k)
            headButtons[k].setToggleState ((mask & (1 << k)) != 0, dontSendNotification);
    });

    for (int k = 0; k < ebx::kNumHeads; ++k)
    {
        auto& button = headButtons[k];
        button.setButtonText (String (k + 1));
        button.getProperties().set ("plain", true);
        button.onClick = [this, k]
        {
            const auto mask = ebx::headMask (roundToInt (processor.apvts.getRawParameterValue ("heads")->load())) ^ (1 << k);

            if (mask != 0)
                headsAttachment->setValueAsCompleteGesture ((float) (mask - 1));
            else
                headsAttachment->sendInitialUpdate();
        };
        addAndMakeVisible (button);
    }

    headsAttachment->sendInitialUpdate();

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, "out", output));
    addAndMakeVisible (output);

    // What each control does, for a pointer that rests on it.
    describe ({ &presetBox }, "Le preset chargé. Un clic ouvre le navigateur : les presets en colonnes, à filtrer par origine et par type d'écho, ou à chercher par nom.");
    describe ({ &previousButton, &nextButton }, "Le preset d'avant, le preset d'après, parmi ceux que les filtres du navigateur laissent passer.");
    describe ({ &saveButton }, "Enregistre les réglages comme un preset à toi : tape son nom, puis Entrée.");
    describe ({ &deleteButton }, "Supprime le preset affiché, si c'est l'un des tiens. Il demande une confirmation.");
    describe ({ &slotAButton, &slotBButton }, "A et B : deux réglages côte à côte, pour comparer.");
    describe ({ &copyButton }, "Copie les réglages qu'on écoute dans l'autre slot.");
    describe ({ &bypassButton }, "Bypass : l'entrée telle qu'elle est arrivée.");
    describe ({ &display }, "À gauche, la machine : une puce numérique qui devient un magnéto à bande quand le caractère tourne vers TAPE, avec ses têtes de lecture. À droite, les répétitions qu'un son va produire : une marque par écho, aussi haute qu'il revient fort, la gauche au-dessus, la droite en dessous. Un curseur part de la dernière note jouée et allume chaque écho au moment où il sonne. En bas, ce qui vient de sortir.");

    describe ({ &time }, "Time : le temps entre le son et son premier écho (tête 1). Avec Sync, une valeur de note calée sur le tempo du morceau.");
    describe ({ &syncButton }, "Sync : cale le temps sur le tempo du DAW, en valeurs de notes (1/4, 1/8 pointée, triolets...).");
    describe ({ &feedback }, "Feedback : combien de fois l'écho revient. Au-delà de 100 %, il s'emballe et chante tout seul : la bande sature doucement, le numérique sature franchement.");
    describe ({ &mix }, "Mix : de l'entrée seule (0) aux échos seuls (100). Au milieu, les deux à plein.");
    describe ({ &delayOut }, "Delay Out : le niveau des échos seuls, de -30 à +6 dB, sans toucher au son direct. En insert, baisse-le pour doser finement la quantité de delay ; Out règle tout ce qui sort.");
    describe ({ &headButtons[0], &headButtons[1], &headButtons[2] }, "Les têtes de lecture, comme sur un écho à bande multi-têtes : la tête 1 lit au temps choisi, la 2 au double, la 3 au triple. Plusieurs à la fois donnent des rythmes ; elles se renvoient toutes dans le feedback.");
    describe ({ &freezeButton }, "Freeze : fige ce qui est sur la bande et le fait tourner en boucle, sans perte, pendant que tu joues par-dessus.");
    describe ({ &character }, "Character : le voyage du delay. Tout à gauche, un numérique pur : chaque répétition est la copie exacte du son, sans filtre, sans couleur, et le temps change net. En allant vers le milieu, les répétitions s'adoucissent à peine. Vers les trois quarts, une bande chaude qui sature doucement ; tout à droite, une bande usée, sombre, saturée, qui pleure.");
    describe ({ &tone }, "Tone : la couleur des répétitions. À gauche, de plus en plus sombres à chaque passage ; à droite, plus brillantes et plus fines.");
    describe ({ &pitch }, "Pitch : chaque répétition monte ou descend de ce nombre de demi-tons, à la façon des pitch shifters numériques des années 80 : +12 grimpe d'octave en octave, -5 tombe en cascade.");
    describe ({ &wobble }, "Wobble : l'instabilité du temps. Côté bande, le pleurage lent et le scintillement rapide d'un moteur fatigué ; côté numérique, une modulation régulière et nette, gauche et droite opposées, comme un chorus.");
    describe ({ &pingPong }, "Ping-Pong : à 0, chaque côté a ses propres échos ; à 100, ils passent d'un côté à l'autre, gauche, droite, gauche...");
    describe ({ &space }, "Space : la réverbe dans laquelle tombent les répétitions. Côté bande, elle prend le son d'un ressort.");
    describe ({ &output }, "Out : le niveau de tout ce qui sort.");
    hoverTip.setInterceptsMouseClicks (false, false);
    hoverTip.setAlwaysOnTop (true);
    addChildComponent (hoverTip);

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ECHOBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);

    setResizable (true, true);
    setResizeLimits (980, 620, 2200, 1300);
    // taille d'ouverture : la règle commune des plugins ONIRIQUE (ui/OniriqueWindowSize.h)
    const auto opening = onirique::window::openingSize ({}, "ONIRIQUE ECHOBOX", { 1160, 720 }, { 980, 620 }, { 2200, 1300 });
    setSize (opening.x, opening.y);

    timerCallback();
    startTimerHz (30);
}

EchoBoxEditor::~EchoBoxEditor()
{
    onirique::window::rememberOnThisComputer ("ONIRIQUE ECHOBOX", getWidth(), getHeight());
    setLookAndFeel (nullptr);
}

void EchoBoxEditor::attachTime (bool shouldSync)
{
    if ((int) shouldSync == synced)
        return;

    synced = (int) shouldSync;
    time.attach (processor.apvts, shouldSync ? "division" : "time");

    if (shouldSync)
        time.setEnds ("1/32", "1/1");
    else
        time.setEnds ("20ms", "2s");

    time.repaint();
}

//==============================================================================
void EchoBoxEditor::refreshPresets()
{
    // What the two arrows step through: the presets the browser is narrowed to.
    presetNames = processor.getFilteredPresets();
}

void EchoBoxEditor::stepPreset (int direction)
{
    if (presetNames.isEmpty())
        return;

    const auto count = presetNames.size();
    const auto current = presetNames.indexOf (processor.getPresetName());
    const auto next = current < 0 ? 0 : (current + direction + count) % count;

    processor.loadPreset (presetNames[next]);
}

void EchoBoxEditor::beginSaving()
{
    const auto current = processor.getPresetName();

    presetNameEditor.setText ({}, dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.showEditor();

    // The name on show is there, selected: Enter keeps it, typing replaces
    // it, Escape gives up. A stock preset's name comes back with "(user)".
    if (auto* editor = presetNameEditor.getCurrentTextEditor())
    {
        // Where the menu wrote it: the same inset, on the middle line.
        editor->setBorder ({ 0, 13, 0, 30 });
        editor->setIndents (0, 0);
        editor->setJustification (Justification::centredLeft);
        editor->setText (current, false);
        editor->selectAll();
    }
}

void EchoBoxEditor::deleteClicked()
{
    // Deleting is for good, so it takes two clicks: the first asks.
    if (deleteArmed == 0)
    {
        deleteArmed = 90;
        return;
    }

    deleteArmed = 0;
    processor.deleteUserPreset (processor.getPresetName());
    refreshPresets();
}

void EchoBoxEditor::timerCallback()
{
    // What a preset, the other slot or the host may have changed under us.
    const auto name = processor.getPresetName();

    if (presetBox.getText() != name)
        presetBox.setText (name, dontSendNotification);

    if (browser.isVisible())
        browser.repaint();

    if (deleteArmed > 0)
        --deleteArmed;

    deleteButton.setEnabled (processor.isUserPreset (name));
    deleteButton.setButtonText (deleteArmed > 0 ? "SURE ?" : "DELETE");
    deleteButton.setToggleState (deleteArmed > 0, dontSendNotification);

    const auto slot = processor.getActiveSlot();

    slotAButton.setToggleState (slot == 0, dontSendNotification);
    slotBButton.setToggleState (slot == 1, dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "A > B" : "B > A");

    attachTime (processor.apvts.getRawParameterValue ("sync")->load() > 0.5f);

    // The character dial wears the colour of where it stands.
    const auto c = (float) character.slider.getValue() * 0.01f;

    if (std::abs (c - shownCharacter) > 0.001f)
    {
        shownCharacter = c;
        character.setAccent (EchoDisplay::characterColour (c));
    }

    // What counts for nothing as things stand is drawn fainter: the crossing
    // of repeats that are not fed back.
    pingPong.setAlpha (feedback.slider.getValue() > 0.0 ? 1.0f : 0.4f);

    display.repaint();
    updateHoverTip();

    // The header is the only part of the frame that moves by itself.
    repaint (0, 0, getWidth(), kHeaderHeight);
}

void EchoBoxEditor::describe (std::initializer_list<Component*> components, const char* text)
{
    // The descriptions are written in French, accents and all.
    for (auto* c : components)
        described.push_back ({ c, String::fromUTF8 (text) });
}

void EchoBoxEditor::updateHoverTip()
{
    const auto now = Time::getMillisecondCounter();
    const auto at = getMouseXYRelative();

    // Only while the pointer is over this window with no button held.
    auto* under = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    const auto here = under != nullptr && (under == this || isParentOf (under))
                      && ! ModifierKeys::currentModifiers.isAnyMouseButtonDown();

    int over = -1;

    // Nothing under the browser is being pointed at.
    if (here && ! browser.isVisible())
        for (int i = 0; i < (int) described.size(); ++i)
            if (auto* c = described[(size_t) i].component; c->isShowing() && getLocalArea (c, c->getLocalBounds()).contains (at))
                over = i;

    // Leaving the control takes the description away, and starts the wait
    // for the next one.
    if (over != resting)
    {
        resting = over;
        restingAt = at;
        restingSince = now;
        hoverTip.setVisible (false);
        return;
    }

    // Once it is there it stays for as long as the pointer is on the
    // control, moving or not.
    if (resting < 0 || hoverTip.isVisible())
        return;

    // Until then, any movement starts the two seconds again.
    if (at.getDistanceFrom (restingAt) > 3)
    {
        restingAt = at;
        restingSince = now;
        return;
    }

    if (now - restingSince < 2000)
        return;

    const auto& text = described[(size_t) resting].text;

    hoverTip.text = text;
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (text, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (true);
    hoverTip.toFront (false);
    hoverTip.repaint();
}

int EchoBoxEditor::titleRight() const
{
    return kTitleLeft
           + roundToInt (GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE")
                         + GlyphArrangement::getStringWidth (titleFont (false), "ECHOBOX"))
           + 12;
}

void EchoBoxEditor::paintHeader (Graphics& g, Rectangle<int> header)
{
    const auto area = header.toFloat();
    const auto seconds = (Time::getMillisecondCounterHiRes() - openedAt) * 0.001;

    // The crimson of the range: darkest at the sigil, brightest behind the
    // name, and falling away again to the right and towards the foot.
    ColourGradient ground (Colour (0xff2c060b), 0.0f, 0.0f, Colour (0xff3b080e), area.getRight(), 0.0f, false);
    ground.addColour (0.32, Colour (0xff7a0e1c));
    ground.addColour (0.7, Colour (0xff5a0a14));
    g.setGradientFill (ground);
    g.fillRect (area);

    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, area.getY(),
                                       Colour (0x73000000), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (logo.isValid())
    {
        const auto logoWidth = (float) logo.getWidth() * 0.5f;
        const auto logoHeight = (float) logo.getHeight() * 0.5f;

        g.drawImage (logo, { 16.0f - (float) kLogoMargin, area.getCentreY() - logoHeight * 0.5f, logoWidth, logoHeight });
    }

    // The name: the house in white, the product in a paler weight beside it.
    const auto nameWidth = GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE");

    g.setColour (Colours::white);
    g.setFont (titleFont (true));
    g.drawText ("ONIRIQUE", area.withTrimmedLeft ((float) kTitleLeft).withWidth (nameWidth + 4.0f),
                Justification::centredLeft, false);

    g.setColour (Colour (0xffe3b5ba));
    g.setFont (titleFont (false));
    g.drawText ("ECHOBOX", area.withTrimmedLeft ((float) kTitleLeft + nameWidth + 10.0f).withWidth (280.0f),
                Justification::centredLeft, false);

    // Stars beside it, each breathing at its own pace.
    struct Spark { float x, y, radius, rate, offset; };
    static const Spark sparks[] = { { 10.0f, 17.0f, 5.5f, 0.9f, 0.0f },  { 27.0f, 11.0f, 3.0f, 1.3f, 1.7f },
                                    { 38.0f, 24.0f, 4.0f, 0.7f, 3.1f },  { 18.0f, 42.0f, 4.5f, 1.1f, 4.4f },
                                    { 34.0f, 48.0f, 2.5f, 1.6f, 2.3f },  { 46.0f, 38.0f, 2.0f, 1.0f, 5.2f } };

    const auto sparkLeft = (float) titleRight();

    for (const auto& s : sparks)
    {
        const auto glow = 0.5f + 0.5f * (float) std::sin (seconds * s.rate * 2.0 + s.offset);

        g.setColour (Colours::white.withAlpha (0.25f + 0.65f * glow));
        g.fillPath (star ({ sparkLeft + s.x, area.getY() + s.y }, s.radius * (0.8f + 0.2f * glow)));
    }

    // The foot of the header: a faint rule, and a streak of light that runs
    // along it from left to right and comes round again.
    const auto footY = area.getBottom() - 2.0f;

    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (0.0f, footY, area.getWidth(), 1.0f);

    const auto length = jmax (160.0f, area.getWidth() * 0.22f);
    const auto cycle = 3.6;
    const auto travelled = (float) (std::fmod (seconds, cycle) / cycle);
    const auto head = -40.0f + travelled * (area.getWidth() + length + 80.0f);
    const auto tail = head - length;

    // Bright at the head, trailing off behind it, with a halo around the lot.
    ColourGradient halo (Colours::transparentBlack, tail, 0.0f, Colours::transparentBlack, head + 14.0f, 0.0f, false);
    halo.addColour (0.82, ember.withAlpha (0.55f));
    g.setGradientFill (halo);
    g.fillRect (tail, footY - 3.0f, length + 14.0f, 7.0f);

    ColourGradient streak (Colours::transparentWhite, tail, 0.0f, Colours::transparentWhite, head + 6.0f, 0.0f, false);
    streak.addColour (0.55, emberLit.withAlpha (0.9f));
    streak.addColour (0.93, Colours::white);
    g.setGradientFill (streak);
    g.fillRoundedRectangle (tail, footY - 0.5f, length + 6.0f, 2.0f, 1.0f);
}

void EchoBoxEditor::paint (Graphics& g)
{
    g.fillAll (night);

    paintHeader (g, getLocalBounds().removeFromTop (kHeaderHeight));

    // Each family on its own ground, a shade redder than the rest, under
    // its name.
    for (int i = 0; i < kNumPanels; ++i)
    {
        const auto panel = panels[i].toFloat().reduced (0.5f);

        g.setColour (deep);
        g.fillRoundedRectangle (panel, 10.0f);
        g.setColour (line);
        g.drawRoundedRectangle (panel, 10.0f, 1.0f);

        g.setColour (haze);
        g.setFont (capsFont (9.5f).withExtraKerningFactor (0.3f));
        g.drawText (kPanelNames[i], panels[i].withHeight (kPanelHeading).translated (0, 2), Justification::centred, false);
    }

    g.setColour (mist);
    g.setFont (capsFont (10.0f));
    g.drawText ("HEADS", headsHeading, Justification::centred, false);
}

Rectangle<int> EchoBoxEditor::cell (int panel, int row, float column) const
{
    auto area = panels[panel].reduced (8, 0).withTrimmedTop (kPanelHeading).withTrimmedBottom (10);
    const auto rowHeight = (area.getHeight() - 8) / 2;
    const auto width = (float) area.getWidth() / (float) kPanelColumns[panel];

    return { area.getX() + roundToInt (column * width), area.getY() + row * (rowHeight + 8), roundToInt (width), rowHeight };
}

void EchoBoxEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderHeight);

    // Controls sit in the upper part of the header, clear of the light that
    // runs along its foot.
    auto row = header.withTrimmedBottom (6).withSizeKeepingCentre (header.getWidth(), 30).withTrimmedRight (14);

    bypassButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (10);

    // The licence badge is one of the header's buttons, next to Bypass: the same height, the same capsule.
    // Its room follows its wording, and the header is laid out again when the licence changes.
    const auto badge = row.removeFromRight (licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0);
    row.removeFromRight (18);

    copyButton.setBounds (row.removeFromRight (64));
    row.removeFromRight (4);
    slotBButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (4);
    slotAButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (18);

    deleteButton.setBounds (row.removeFromRight (74));
    row.removeFromRight (4);
    saveButton.setBounds (row.removeFromRight (60));
    row.removeFromRight (8);

    // The name takes what is left between the title and the buttons.
    row.removeFromLeft (titleRight() + 62);
    previousButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (6);
    presetBox.setBounds (row);
    presetNameEditor.setBounds (row);

    auto foot = bounds.removeFromBottom (kFootHeight).reduced (14, 0).withTrimmedBottom (12).withTrimmedTop (4);
    output.setBounds (foot.removeFromRight (100));

    // The window grows by giving its contents more room, not by zooming them: the scale is 1.
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), 1.0f, badge, Justification::centredRight);

    // The families take the lower half, the screen what is left above them.
    auto lower = bounds.removeFromBottom (jlimit (280, 380, bounds.getHeight() * 50 / 100)).reduced (14, 0);
    const auto unit = (lower.getWidth() - (kNumPanels - 1) * kPanelGap) / kColumns;

    for (int i = 0; i < kNumPanels; ++i)
    {
        panels[i] = i == kNumPanels - 1 ? lower : lower.removeFromLeft (unit * kPanelColumns[i]);
        lower.removeFromLeft (kPanelGap);
    }

    display.setBounds (bounds.reduced (14, 12));

    // The browser lies over everything under the header.
    browser.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (14, 12));

    // The echo: three dials over their switches.
    time.setBounds (cell (0, 0, 0));
    feedback.setBounds (cell (0, 0, 1));
    mix.setBounds (cell (0, 0, 2));
    delayOut.setBounds (cell (0, 0, 3));

    auto button = [] (Rectangle<int> area, int width) { return area.withSizeKeepingCentre (jmin (width, area.getWidth() - 12), 30); };

    syncButton.setBounds (button (cell (0, 1, 0), 84));
    freezeButton.setBounds (button (cell (0, 1, 3), 96));

    auto heads = cell (0, 1, 1.5f);
    headsHeading = heads.withSizeKeepingCentre (heads.getWidth(), 15).translated (0, -24);
    auto strip = button (heads, 3 * 30 + 2 * 6);

    for (auto& b : headButtons)
    {
        b.setBounds (strip.removeFromLeft (30));
        strip.removeFromLeft (6);
    }

    // The character, alone and large.
    character.setBounds (cell (1, 0, 0).getUnion (cell (1, 1, 1)).reduced (16, 6));

    // The colour: three over two.
    tone.setBounds (cell (2, 0, 0));
    pitch.setBounds (cell (2, 0, 1));
    wobble.setBounds (cell (2, 0, 2));
    pingPong.setBounds (cell (2, 1, 0.5f));
    space.setBounds (cell (2, 1, 1.5f));
}

} // namespace onirique
