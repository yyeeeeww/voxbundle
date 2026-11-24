#include "HeatProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HeatProcessor(); }
