#include "SpaceProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpaceProcessor(); }
