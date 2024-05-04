/// @file
///	@ingroup 	grainflow
///	@copyright	Copyright 2024 Christopher Poovey
///	@license	Use of this source code is governed by the MIT License found in the License.md file.
///
///
///

#include "Grainflow.voice_tilde.h"

using namespace c74::min;
using namespace Grainflow;

long simplemc_multichanneloutputs(c74::max::t_object* x, long index, long count);
long simplemc_inputchanged(c74::max::t_object* x, long index, long count);



int grainflow_voice_tilde::GetMaxGrains() { return maxGrains; }

grainflow_voice_tilde::~grainflow_voice_tilde()
{
	Cleanup();
}
#pragma region DSP

void grainflow_voice_tilde::operator()(audio_bundle input, audio_bundle output)
{
	_ioConfig.livemode = _livemode;
	_ioConfig.in = input.samples();
	_ioConfig.out = output.samples();

	// These varible indicate the starting indices of each mc parameter
	_ioConfig.grainClockCh = 0;
	_ioConfig.traversalPhasorCh = input_chans[0];
	_ioConfig.fmCh = _ioConfig.traversalPhasorCh + input_chans[1];
	_ioConfig.amCh = _ioConfig.fmCh + input_chans[2];

	// Ouputs are constant because they are based on the max grain count
	_ioConfig.grainOutput = 0;
	_ioConfig.grainState = 1 * maxGrains;
	_ioConfig.grainProgress = 2 * maxGrains;
	_ioConfig.grainPlayhead = 3 * maxGrains;
	_ioConfig.grainAmp = 4 * maxGrains;
	_ioConfig.grainEnvelope = 5 * maxGrains;
	_ioConfig.grainBufferChannel = 6 * maxGrains;
	_ioConfig.grainStreamChannel = 7 * maxGrains;


	// Clear unused channels or we will get garbage
	_ioConfig.blockSize = output.frame_count();
	_ioConfig.samplerate = samplerate();
	for (int g = 0; g < (maxGrains) * 8; g++)
	{
		memset(_ioConfig.out[g], double(0), sizeof(double) * _ioConfig.blockSize);
	}

	for (int g = 0; g < _ngrains; g++)
	{
		_ioConfig.grainClock = _ioConfig.grainClockCh + (g % input_chans[0]);
		_ioConfig.traversalPhasor = _ioConfig.traversalPhasorCh + (g % input_chans[1]);
		_ioConfig.fm = _ioConfig.fmCh + (g % input_chans[2]);
		_ioConfig.am = _ioConfig.amCh + (g % input_chans[3]);

		if (_ioConfig.blockSize < INTERNALBLOCK) return;

		auto thisGrain = &grainInfo[g];

		buffer_lock<> grainSamples(*(thisGrain->GetBuffer(GFBuffers::buffer))); //These locks must be in the outer scope
		buffer_lock<> envelopeSamples(*(thisGrain->GetBuffer(GFBuffers::envelope)));

		float* envelopeBuffer = emptyBuffer;
		int envelopeFrames = 1;
		if (envelopeSamples.valid()) {
			envelopeBuffer = &envelopeSamples[0];
			envelopeFrames = envelopeSamples.frame_count();
		}

		float* grainBuffer = emptyBuffer;
		int grainFrames = 1;
		int grainNChannels = 1;
		int grainSr = samplerate();
		if (grainSamples.valid()) {
			grainBuffer = &grainSamples[0];
			grainFrames = grainSamples.frame_count();
			grainNChannels = grainSamples.channel_count();
			grainSr = grainSamples.samplerate();
		}

		thisGrain->Proccess(_ioConfig, grainBuffer, envelopeBuffer, grainFrames, grainNChannels, grainSr, envelopeFrames);
		
	}
}

#pragma endregion



/// <summary>
/// Helper to make targeting grains easier
/// </summary>
/// <param name="value"></param>
/// <param name="param"></param>
/// <param name="type"></param>
void grainflow_voice_tilde::GrainMessage(float value, GfParamName param, GfParamType type)
{
	if (_streamTarget > 0)
	{
		for (int g = 0; g < maxGrains; g++)
		{
			if (grainInfo[g].stream + 1 != _streamTarget)
				continue;
			grainInfo[g].ParamSet(value, param, type);
		}
		return;
	}

	if (_channelTarget > 0)
	{
		for (int g = 0; g < maxGrains; g++)
		{
			if (grainInfo[g].bchan + 1 != _channelTarget)
				continue;
			grainInfo[g].ParamSet(value, param, type);
		}
		return;
	}

	if (_target > 0)
	{
		if (_target >= maxGrains)
			return;
		for (int g = 0; g < maxGrains; g++)
		{
			grainInfo[g].ParamSet(value, param, type);
		}

		return;
	}
	for (int g = 0; g < maxGrains; g++)
	{
		grainInfo[g].ParamSet(value, param, type);
	}
};

void grainflow_voice_tilde::BufferRefMessage(string bname, GFBuffers type)
{
	if (bname.empty())
		return;

	if (_target > 0)
	{
		auto buf = grainInfo[_target - 1].GetBuffer(type);
		buf->set(""); // This forces a refresh even if the name is the same
		buf->set(bname);
		return;
	}
	for (int g = 0; g < maxGrains; g++)
	{
		auto buf = grainInfo[g].GetBuffer(type); // To access ir must be converted to the correct type
		buf->set("");
		buf->set(bname);
	}
}
/// <summary>
/// Forces a refresh of a type of buffer.
/// </summary>
void grainflow_voice_tilde::BufferRefresh(GFBuffers type)
{
	for (int g = 0; g < maxGrains; g++)
	{
		auto buf = grainInfo[g].GetBuffer(type); // To access ir must be converted to the correct type
		auto name = buf->name();
		buf->set("");
		buf->set(name);
	}
};

void grainflow_voice_tilde::Init()
{
	for (int g = 0; g < maxGrains; g++)
	{
		grainInfo[g].SetIndex(g);
		grainInfo[g].SetBuffer(GFBuffers::buffer, (new buffer_reference(this, nullptr, false)));
		grainInfo[g].SetBuffer(GFBuffers::envelope, (new buffer_reference(this, nullptr, false)));
		grainInfo[g].SetBuffer(GFBuffers::delayBuffer, (new buffer_reference(this, nullptr, false)));
		grainInfo[g].SetBuffer(GFBuffers::windowBuffer, (new buffer_reference(this, nullptr, false)));
		grainInfo[g].SetBuffer(GFBuffers::rateBuffer, (new buffer_reference(this, nullptr, false)));

		auto env = grainInfo[g].GetBuffer(GFBuffers::envelope);
		env->set(envArg);
		auto buf = grainInfo[g].GetBuffer(GFBuffers::buffer);
		buf->set(bufferArg);
	}
}

void grainflow_voice_tilde::Cleanup()
{
	grainInfo.reset();
}

void grainflow_voice_tilde::Reinit(int grains)
{
	Cleanup();
	grainInfo = std::unique_ptr<MspGrain<INTERNALBLOCK>[]>(new MspGrain<INTERNALBLOCK>[grains]);
	maxGrains = grains;
	Init();
}

#pragma region MAX_API_EX
/// <summary>
/// Allows for the use of mc outlets. Must be added as an event at the objects startup
/// </summary>
/// <param name="x"></param>
/// <param name="index"></param>
/// <param name="count"></param>
/// <returns></returns>
long simplemc_multichanneloutputs(c74::max::t_object* x, long index, long count)
{
	minwrap<grainflow_voice_tilde>* ob = (minwrap<grainflow_voice_tilde> *)(x);
	return ob->m_min_object.GetMaxGrains();
}
/// <summary>
/// Allows for the use of mc inputs. Must be added as an event at the objects startup. Also requires an input channels regester
/// </summary>
/// <param name="x"></param>
/// <param name="index"></param>
/// <param name="count"></param>
/// <returns></returns>
long simplemc_inputchanged(c74::max::t_object* x, long index, long count)
{
	minwrap<grainflow_voice_tilde>* ob = (minwrap<grainflow_voice_tilde> *)(x);
	// auto chan_number = ob->m_min_object.GetMaxGrains(); //We should check for bonus channels and handle it
	ob->m_min_object.input_chans[index] = count > 0 ? count : 1; // Tells us how many channels are in each inlet
	return false;
}
#pragma endregion

MIN_EXTERNAL(grainflow_voice_tilde);