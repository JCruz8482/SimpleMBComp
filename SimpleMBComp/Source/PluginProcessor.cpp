/*
  ==============================================================================

		This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"

//==============================================================================
SimpleMBCompAudioProcessor::SimpleMBCompAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor(
		BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
	)
#endif
{
	using namespace Params;
	const auto& params = GetParams();

	auto floatHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
	{
		param = dynamic_cast<AudioParameterFloat*>(apvts.getParameter(params.at(paramName)));
		jassert(param != nullptr);
	};

	floatHelper(lowBandComp.attack, Names::Attack_Low_Band);
	floatHelper(lowBandComp.release, Names::Release_Low_Band);
	floatHelper(lowBandComp.threshold, Names::Threshold_Low_Band);

	floatHelper(midBandComp.attack, Names::Attack_Mid_Band);
	floatHelper(midBandComp.release, Names::Release_Mid_Band);
	floatHelper(midBandComp.threshold, Names::Threshold_Mid_Band);

	floatHelper(highBandComp.attack, Names::Attack_High_Band);
	floatHelper(highBandComp.release, Names::Release_High_Band);
	floatHelper(highBandComp.threshold, Names::Threshold_High_Band);

	floatHelper(lowBandComp.ratio, Names::Ratio_Low_Band);
	floatHelper(midBandComp.ratio, Names::Ratio_Mid_Band);
	floatHelper(highBandComp.ratio, Names::Ratio_High_Band);

	auto boolHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
	{
		param = dynamic_cast<AudioParameterBool*>(apvts.getParameter(params.at(paramName)));
		jassert(param != nullptr);
	};

	boolHelper(lowBandComp.isBypassed, Names::Bypassed_Low_Band);
	boolHelper(midBandComp.isBypassed, Names::Bypassed_Mid_Band);
	boolHelper(highBandComp.isBypassed, Names::Bypassed_High_Band);

	boolHelper(lowBandComp.isMuted, Names::Mute_Low_Band);
	boolHelper(midBandComp.isMuted, Names::Mute_Mid_Band);
	boolHelper(highBandComp.isMuted, Names::Mute_High_Band);

	boolHelper(lowBandComp.isSoloed, Names::Solo_Low_Band);
	boolHelper(midBandComp.isSoloed, Names::Solo_Mid_Band);
	boolHelper(highBandComp.isSoloed, Names::Solo_High_Band);

	floatHelper(lowMidCrossover, Names::Low_Mid_Crossover_Freq);
	floatHelper(midHighCrossover, Names::Mid_High_Crossover_Freq);

	LP1.setType(LinkwitzRileyFilterType::lowpass);
	HP1.setType(LinkwitzRileyFilterType::highpass);
	AP2.setType(LinkwitzRileyFilterType::allpass);
	LP2.setType(LinkwitzRileyFilterType::lowpass);
	HP2.setType(LinkwitzRileyFilterType::highpass);
}

SimpleMBCompAudioProcessor::~SimpleMBCompAudioProcessor() {}

//==============================================================================
const juce::String SimpleMBCompAudioProcessor::getName() const {
	return JucePlugin_Name;
}

bool SimpleMBCompAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool SimpleMBCompAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool SimpleMBCompAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
	return true;
#else
	return false;
#endif
}

double SimpleMBCompAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SimpleMBCompAudioProcessor::getNumPrograms() {
	return 1;  // NB: some hosts don't cope very well if you tell them there are 0
			   // programs,
	// so this should be at least 1, even if you're not really implementing
	// programs.
}

int SimpleMBCompAudioProcessor::getCurrentProgram() { return 0; }

void SimpleMBCompAudioProcessor::setCurrentProgram(int index) {}

const juce::String SimpleMBCompAudioProcessor::getProgramName(int index) {
	return {};
}

void SimpleMBCompAudioProcessor::changeProgramName(
	int index, const juce::String& newName) {}

//==============================================================================
void SimpleMBCompAudioProcessor::prepareToPlay(double sampleRate,
	int samplesPerBlock) {
// Use this method as the place to do any pre-playback
// initialisation that you need..
	ProcessSpec spec;

	spec.maximumBlockSize = samplesPerBlock;
	spec.numChannels = getTotalNumOutputChannels();
	spec.sampleRate = sampleRate;

	for (auto& comp : compressors)
		comp.prepare(spec);

	LP1.prepare(spec);
	HP1.prepare(spec);
	AP2.prepare(spec);
	LP2.prepare(spec);
	HP2.prepare(spec);

	for (auto& buffer : filterBuffers)
	{
		buffer.setSize(spec.numChannels, samplesPerBlock);
	}
}

void SimpleMBCompAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleMBCompAudioProcessor::isBusesLayoutSupported(
	const BusesLayout& layouts) const {
#if JucePlugin_IsMidiEffect
	juce::ignoreUnused(layouts);
	return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts.
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
		layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

		// This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
#endif

	return true;
#endif
}
#endif

void SimpleMBCompAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
	juce::MidiBuffer& midiMessages) {
	juce::ScopedNoDenormals noDenormals;
	auto totalNumInputChannels = getTotalNumInputChannels();
	auto totalNumOutputChannels = getTotalNumOutputChannels();

	// In case we have more outputs than inputs, this code clears any output
	// channels that didn't contain input data, (because these aren't
	// guaranteed to be empty - they may contain garbage).
	// This is here to avoid people getting screaming feedback
	// when they first compile a plugin, but obviously you don't need to keep
	// this code if your algorithm always overwrites all the output channels.
	for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
		buffer.clear(i, 0, buffer.getNumSamples());

	for (auto& comp : compressors)
		comp.updateCompressorSettings();

	for (auto& fb : filterBuffers)
	{
		fb = buffer;
	}

	auto lowMidCutoff = lowMidCrossover->get();
	LP1.setCutoffFrequency(lowMidCutoff);
	HP1.setCutoffFrequency(lowMidCutoff);

	auto midHighCutoff = midHighCrossover->get();
	AP2.setCutoffFrequency(midHighCutoff);
	LP2.setCutoffFrequency(midHighCutoff);
	HP2.setCutoffFrequency(midHighCutoff);

	auto fb0Block = AudioBlock<float>(filterBuffers[0]);
	auto fb1Block = AudioBlock<float>(filterBuffers[1]);
	auto fb2Block = AudioBlock<float>(filterBuffers[2]);

	auto fb0Ctx = ProcessContextReplacing<float>(fb0Block);
	auto fb1Ctx = ProcessContextReplacing<float>(fb1Block);
	auto fb2Ctx = ProcessContextReplacing<float>(fb2Block);

	LP1.process(fb0Ctx);
	AP2.process(fb0Ctx);
	HP1.process(fb1Ctx);
	filterBuffers[2] = filterBuffers[1];
	LP2.process(fb1Ctx);
	HP2.process(fb2Ctx);

	for (size_t i = 0; i < filterBuffers.size(); ++i)
		compressors[i].process(filterBuffers[i]);

	auto numSamples = buffer.getNumSamples();
	auto numChannels = buffer.getNumChannels();

	buffer.clear();

	auto addFilterBand = [nc = numChannels, ns = numSamples](auto& inputBuffer, const auto& source)
	{
		for (auto i = 0; i < nc; ++i)
		{
			inputBuffer.addFrom(i, 0, source, i, 0, ns);
		}
	};

	bool isAnySoloed = false;

	for (auto& comp : compressors)
	{
		if (comp.isSoloed->get())
		{
			isAnySoloed = true;
			break;
		}
	}

	if (isAnySoloed)
	{
		for (size_t i = 0; i < compressors.size(); ++i)
		{
			if (compressors[i].isSoloed->get())
			{
				addFilterBand(buffer, filterBuffers[i]);
			}
		}
	}
	else
	{
		for (size_t i = 0; i < compressors.size(); ++i)
		{
			if (!compressors[i].isMuted->get())
			{
				addFilterBand(buffer, filterBuffers[i]);
			}
		}
	}
}

//==============================================================================
bool SimpleMBCompAudioProcessor::hasEditor() const {
	return true;  // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleMBCompAudioProcessor::createEditor() {
  // return new SimpleMBCompAudioProcessorEditor(*this);
	return new GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleMBCompAudioProcessor::getStateInformation(
	juce::MemoryBlock& destData) {
  // You should use this method to store your parameters in the memory block.
  // You could do that either as raw data, or use the XML or ValueTree classes
  // as intermediaries to make it easy to save and load complex data.
	MemoryOutputStream mos(destData, true);
	apvts.state.writeToStream(mos);
}

void SimpleMBCompAudioProcessor::setStateInformation(const void* data,
	int sizeInBytes) {
// You should use this method to restore your parameters from this memory
// block, whose contents will have been created by the getStateInformation()
// call.
	auto tree = ValueTree::readFromData(data, sizeInBytes);
	if (tree.isValid())
	{
		apvts.replaceState(tree);
	}
}

juce::AudioProcessorValueTreeState::ParameterLayout
SimpleMBCompAudioProcessor::createParameterLayout() {

	using namespace Params;

	static const NormalisableRange<float> threshold_range = NormalisableRange<float>(-60, 12, 1, 1);
	static const NormalisableRange<float> attack_release_range = NormalisableRange<float>(5, 500, 1, 1);
	static const NormalisableRange<float> low_mid_crossover_range = NormalisableRange<float>(20, 999, 1, 1);
	static const NormalisableRange<float> mid_high_crossover_range = NormalisableRange<float>(1000, 20000, 1, 1);
	static const NormalisableRange<float> ratio_range = NormalisableRange<float>(1, 100, 0.01f, 0.35f);
	static const float default_threshold = 0;
	static const float default_attack = 50;
	static const float default_release = 250;
	static const float isActive = false;
	static const float default_low_mid_crossover = 600;
	static const float default_mid_high_crossover = 3500;
	static const float default_ratio = 2;

	const auto& params = GetParams();

	APVTS::ParameterLayout layout;

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Threshold_Low_Band),
		params.at(Names::Threshold_Low_Band),
		threshold_range,
		default_threshold));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Threshold_Mid_Band),
		params.at(Names::Threshold_Mid_Band),
		threshold_range,
		default_threshold));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Threshold_High_Band),
		params.at(Names::Threshold_High_Band),
		threshold_range,
		default_threshold));

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Attack_Low_Band),
		params.at(Names::Attack_Low_Band),
		attack_release_range,
		default_attack));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Attack_Mid_Band),
		params.at(Names::Attack_Mid_Band),
		attack_release_range,
		default_attack));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Attack_High_Band),
		params.at(Names::Attack_High_Band),
		attack_release_range,
		default_attack));

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Release_Low_Band),
		params.at(Names::Release_Low_Band),
		attack_release_range,
		default_release));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Release_Mid_Band),
		params.at(Names::Release_Mid_Band),
		attack_release_range,
		default_release));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Release_High_Band),
		params.at(Names::Release_High_Band),
		attack_release_range,
		default_release));

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Ratio_Low_Band),
		params.at(Names::Ratio_Low_Band),
		ratio_range,
		default_ratio));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Ratio_Mid_Band),
		params.at(Names::Ratio_Mid_Band),
		ratio_range,
		default_ratio));
	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Ratio_High_Band),
		params.at(Names::Ratio_High_Band),
		ratio_range,
		default_ratio));

	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Bypassed_Low_Band),
		params.at(Names::Bypassed_Low_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Bypassed_Mid_Band),
		params.at(Names::Bypassed_Mid_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Bypassed_High_Band),
		params.at(Names::Bypassed_High_Band),
		isActive));

	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Mute_Low_Band),
		params.at(Names::Mute_Low_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Mute_Mid_Band),
		params.at(Names::Mute_Mid_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Mute_High_Band),
		params.at(Names::Mute_High_Band),
		isActive));

	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Solo_Low_Band),
		params.at(Names::Solo_Low_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Solo_Mid_Band),
		params.at(Names::Solo_Mid_Band),
		isActive));
	layout.add(std::make_unique<AudioParameterBool>(
		params.at(Names::Solo_High_Band),
		params.at(Names::Solo_High_Band),
		isActive));

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Low_Mid_Crossover_Freq),
		params.at(Names::Low_Mid_Crossover_Freq),
		low_mid_crossover_range,
		default_low_mid_crossover));

	layout.add(std::make_unique<AudioParameterFloat>(
		params.at(Names::Mid_High_Crossover_Freq),
		params.at(Names::Mid_High_Crossover_Freq),
		mid_high_crossover_range,
		default_mid_high_crossover));

	return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
	return new SimpleMBCompAudioProcessor();
}
