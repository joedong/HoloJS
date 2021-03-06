#include "pch.h"
#include "AudioContext.h"
#include "AudioBufferSourceNode.h"
#include "AudioDestinationNode.h"
#include "AudioNode.h"
#include "GainNode.h"
#include "PannerNode.h"
#include "ScriptHostUtilities.h"
#include "ScriptResourceTracker.h"
#include "SoundBuffer.h"

using namespace HologramJS::Audio;
using namespace HologramJS::Utilities;
using namespace Windows::UI::Core;
using namespace Windows::ApplicationModel::Core;
using namespace std;
using namespace concurrency;

AudioContext::AudioContext() { m_context = lab::MakeAudioContext(); }

AudioContext::~AudioContext() { lab::CleanupAudioContext(m_context); }

bool AudioContext::InitializeProjections()
{
    RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"createGain", L"audioContext", createGainStatic));
    RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"getDestination", L"audioContext", getDestination));
    RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"createBufferSource", L"audioContext", createBufferSource));
    RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"createPanner", L"audioContext", createPanner));

    // Listener functions
    RETURN_IF_FALSE(
        ScriptHostUtilities::ProjectFunction(L"listener_setPosition", L"audioContext", listener_setPosition));
    RETURN_IF_FALSE(
        ScriptHostUtilities::ProjectFunction(L"listener_setOrientation", L"audioContext", listener_setOrientation));

    return true;
}

JsValueRef CHAKRA_CALLBACK AudioContext::listener_setPosition(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 5);

    auto audioContext = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(audioContext);

    const auto x = ScriptHostUtilities::GLfloatFromJsRef(arguments[2]);
    const auto y = ScriptHostUtilities::GLfloatFromJsRef(arguments[3]);
    const auto z = ScriptHostUtilities::GLfloatFromJsRef(arguments[4]);

    audioContext->m_context->listener()->setPosition(x, y, z);

    return JS_INVALID_REFERENCE;
}

JsValueRef CHAKRA_CALLBACK AudioContext::listener_setOrientation(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 8);

    auto audioContext = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(audioContext);

    const auto x = ScriptHostUtilities::GLfloatFromJsRef(arguments[2]);
    const auto y = ScriptHostUtilities::GLfloatFromJsRef(arguments[3]);
    const auto z = ScriptHostUtilities::GLfloatFromJsRef(arguments[4]);

    const auto upX = ScriptHostUtilities::GLfloatFromJsRef(arguments[5]);
    const auto upY = ScriptHostUtilities::GLfloatFromJsRef(arguments[6]);
    const auto upZ = ScriptHostUtilities::GLfloatFromJsRef(arguments[7]);

    audioContext->m_context->listener()->setOrientation(x, y, z, upX, upY, upZ);

    return JS_INVALID_REFERENCE;
}

JsValueRef CHAKRA_CALLBACK AudioContext::createPanner(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 2);

    auto audioContext = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(audioContext);

    auto newPannerNode = new PannerNode(audioContext->m_context,
                                        std::make_shared<lab::PannerNode>(audioContext->m_context->sampleRate()));
    return ScriptResourceTracker::ObjectToDirectExternal(newPannerNode);
}

JsValueRef CHAKRA_CALLBACK AudioContext::createGainStatic(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 2);

    AudioContext* context = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(context);

    return context->createGain();
}

JsValueRef CHAKRA_CALLBACK AudioContext::getDestination(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 2);

    AudioContext* context = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(context);

    return ScriptResourceTracker::ObjectToDirectExternal(
        new AudioDestinationNode(context->m_context, context->m_context->destination()));
}

JsValueRef CHAKRA_CALLBACK AudioContext::createBufferSource(
    JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, PVOID callbackData)
{
    RETURN_IF_FALSE(argumentCount == 2);

    AudioContext* context = ScriptResourceTracker::ExternalToObject<AudioContext>(arguments[1]);
    RETURN_INVALID_REF_IF_NULL(context);

    auto audioBufferSourceNode = make_shared<lab::AudioBufferSourceNode>(context->m_context->sampleRate());

    return ScriptResourceTracker::ObjectToDirectExternal(
        new AudioBufferSourceNode(context->m_context, audioBufferSourceNode));
}

JsValueRef AudioContext::createGain()
{
    auto newGainNode = new GainNode(m_context, std::make_shared<lab::GainNode>(m_context->sampleRate()));
    m_audioNodes.push_back(newGainNode);
    return ScriptResourceTracker::ObjectToDirectExternal(newGainNode);
}

bool AudioContext::DecodeAudioData(JsValueRef data, JsValueRef onSuccess, JsValueRef onError)
{
    JsValueType dataType;
    RETURN_IF_JS_ERROR(JsGetValueType(data, &dataType));
    RETURN_IF_TRUE(dataType != JsArrayBuffer);

    RETURN_IF_JS_ERROR(JsAddRef(onSuccess, nullptr));
    RETURN_IF_JS_ERROR(JsAddRef(data, nullptr));

    if (onError != JS_INVALID_REFERENCE) {
        RETURN_IF_JS_ERROR(JsAddRef(onError, nullptr));
    }

    // Decode async
    create_async([this, data, onSuccess, onError] {
        byte* buffer;
        unsigned int bufferLength;
        if (JsGetArrayBufferStorage(data, &buffer, &bufferLength) == JsNoError) {
            vector<uint8_t> capturedBuffer(bufferLength);
            memcpy(capturedBuffer.data(), buffer, bufferLength);

            // WAV magic: RIFF****WAVE
            PCSTR wavMagic1 = "RIFF";
            PCSTR wavMagic2 = "WAVE";
            PCSTR oggMagic = "OggS";

            PCSTR soundExtension = nullptr;

            if (memcmp(capturedBuffer.data(), wavMagic1, strlen(wavMagic1)) == 0 &&
                memcmp(capturedBuffer.data() + strlen(wavMagic1) + 4, wavMagic2, strlen(wavMagic2)) == 0) {
                soundExtension = "wav";
            } else if (memcmp(capturedBuffer.data(), oggMagic, strlen(oggMagic)) == 0) {
                soundExtension = "ogg";
            }

            if (soundExtension != nullptr) {
                shared_ptr<lab::SoundBuffer> soundBuffer =
                    make_shared<lab::SoundBuffer>(capturedBuffer, soundExtension, m_context->sampleRate());

                CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                    CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, soundBuffer, onSuccess] {
                        callbackScriptOnDecodeSuccess(soundBuffer, onSuccess);
                        JsRelease(onSuccess, nullptr);
                    }));

                if (onError != JS_INVALID_REFERENCE) {
                    JsRelease(onError, nullptr);
                }

                JsRelease(data, nullptr);

                return;
            }
        }

        if (onError != JS_INVALID_REFERENCE) {
            CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                                                                        ref new DispatchedHandler([this, onError] {
                                                                            callbackScriptOnDecodeError(onError);
                                                                            JsRelease(onError, nullptr);
                                                                        }));
        }

        JsRelease(onSuccess, nullptr);
        JsRelease(data, nullptr);
    });

    return true;
}

void AudioContext::callbackScriptOnDecodeError(JsValueRef callback)
{
    EXIT_IF_TRUE(callback == JS_INVALID_REFERENCE);

    JsValueRef result;
    HANDLE_EXCEPTION_IF_JS_ERROR(JsCallFunction(callback, &callback, 1, &result));

    JsRelease(callback, nullptr);
}

void AudioContext::callbackScriptOnDecodeSuccess(shared_ptr<lab::SoundBuffer> soundBuffer, JsValueRef callback)
{
    EXIT_IF_TRUE(callback == JS_INVALID_REFERENCE);

    JsValueRef parameters[2];
    parameters[0] = callback;
    parameters[1] = ScriptResourceTracker::ObjectToDirectExternal(new SoundBuffer(soundBuffer));

    JsValueRef result;
    HANDLE_EXCEPTION_IF_JS_ERROR(JsCallFunction(callback, parameters, ARRAYSIZE(parameters), &result));

    JsRelease(callback, nullptr);
}
