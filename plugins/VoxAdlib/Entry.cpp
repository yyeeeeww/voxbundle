#include "AdlibProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AdlibProcessor(); }
