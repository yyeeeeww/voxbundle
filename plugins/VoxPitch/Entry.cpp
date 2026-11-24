#include "PitchProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PitchProcessor(); }
