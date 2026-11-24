#include "DevilProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DevilProcessor(); }
