/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>
#include <map>
#include <unistd.h>
#include <sys/types.h>

#include <system/audio.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include "AudioPatch.h"
#include "RoutingStrategy.h"

namespace android {

class DeviceDescriptor;
class HwAudioOutputDescriptor;
class SwAudioOutputDescriptor;

class ClientDescriptor: public RefBase
{
public:
    ClientDescriptor(audio_port_handle_t portId, uid_t uid, audio_session_t sessionId,
                   audio_attributes_t attributes, audio_config_base_t config,
                   audio_port_handle_t preferredDeviceId) :
        mPortId(portId), mUid(uid), mSessionId(sessionId), mAttributes(attributes),
        mConfig(config), mPreferredDeviceId(preferredDeviceId), mActive(false) {}
    ~ClientDescriptor() override = default;

    virtual void dump(String8 *dst, int spaces, int index) const;
    virtual std::string toShortString() const;

    audio_port_handle_t portId() const { return mPortId; }
    uid_t uid() const { return mUid; }
    audio_session_t session() const { return mSessionId; };
    audio_attributes_t attributes() const { return mAttributes; }
    audio_config_base_t config() const { return mConfig; }
    audio_port_handle_t preferredDeviceId() const { return mPreferredDeviceId; };
    void setPreferredDeviceId(audio_port_handle_t preferredDeviceId) {
        mPreferredDeviceId = preferredDeviceId;
    };
    void setActive(bool active) { mActive = active; }
    bool active() const { return mActive; }
    bool hasPreferredDevice(bool activeOnly = false) const {
        return mPreferredDeviceId != AUDIO_PORT_HANDLE_NONE && (!activeOnly || mActive);
    }

private:
    const audio_port_handle_t mPortId;  // unique Id for this client
    const uid_t mUid;                     // client UID
    const audio_session_t mSessionId;       // audio session ID
    const audio_attributes_t mAttributes; // usage...
    const audio_config_base_t mConfig;
          audio_port_handle_t mPreferredDeviceId;  // selected input device port ID
          bool mActive;
};

class TrackClientDescriptor: public ClientDescriptor
{
public:
    TrackClientDescriptor(audio_port_handle_t portId, uid_t uid, audio_session_t sessionId,
                   audio_attributes_t attributes, audio_config_base_t config,
                   audio_port_handle_t preferredDeviceId, audio_stream_type_t stream,
                          routing_strategy strategy, audio_output_flags_t flags) :
        ClientDescriptor(portId, uid, sessionId, attributes, config, preferredDeviceId),
        mStream(stream), mStrategy(strategy), mFlags(flags) {}
    ~TrackClientDescriptor() override = default;

    using ClientDescriptor::dump;
    void dump(String8 *dst, int spaces, int index) const override;
    std::string toShortString() const override;

    audio_output_flags_t flags() const { return mFlags; }
    audio_stream_type_t stream() const { return mStream; }
    routing_strategy strategy() const { return mStrategy; }

private:
    const audio_stream_type_t mStream;
    const routing_strategy mStrategy;
    const audio_output_flags_t mFlags;
};

class RecordClientDescriptor: public ClientDescriptor
{
public:
    RecordClientDescriptor(audio_port_handle_t portId, uid_t uid, audio_session_t sessionId,
                        audio_attributes_t attributes, audio_config_base_t config,
                        audio_port_handle_t preferredDeviceId,
                        audio_source_t source, audio_input_flags_t flags, bool isSoundTrigger) :
        ClientDescriptor(portId, uid, sessionId, attributes, config, preferredDeviceId),
        mSource(source), mFlags(flags), mIsSoundTrigger(isSoundTrigger), mSilenced(false) {}
    ~RecordClientDescriptor() override = default;

    using ClientDescriptor::dump;
    void dump(String8 *dst, int spaces, int index) const override;

    audio_source_t source() const { return mSource; }
    audio_input_flags_t flags() const { return mFlags; }
    bool isSoundTrigger() const { return mIsSoundTrigger; }
    void setSilenced(bool silenced) { mSilenced = silenced; }
    bool isSilenced() const { return mSilenced; }

private:
    const audio_source_t mSource;
    const audio_input_flags_t mFlags;
    const bool mIsSoundTrigger;
          bool mSilenced;
};

class SourceClientDescriptor: public TrackClientDescriptor
{
public:
    SourceClientDescriptor(audio_port_handle_t portId, uid_t uid, audio_attributes_t attributes,
                           const sp<AudioPatch>& patchDesc, const sp<DeviceDescriptor>& srcDevice,
                           audio_stream_type_t stream, routing_strategy strategy);
    ~SourceClientDescriptor() override = default;

    sp<AudioPatch> patchDesc() const { return mPatchDesc; }
    sp<DeviceDescriptor> srcDevice() const { return mSrcDevice; };
    wp<SwAudioOutputDescriptor> swOutput() const { return mSwOutput; }
    void setSwOutput(const sp<SwAudioOutputDescriptor>& swOutput);
    wp<HwAudioOutputDescriptor> hwOutput() const { return mHwOutput; }
    void setHwOutput(const sp<HwAudioOutputDescriptor>& hwOutput);

    using ClientDescriptor::dump;
    void dump(String8 *dst, int spaces, int index) const override;

 private:
    const sp<AudioPatch> mPatchDesc;
    const sp<DeviceDescriptor> mSrcDevice;
    wp<SwAudioOutputDescriptor> mSwOutput;
    wp<HwAudioOutputDescriptor> mHwOutput;
};

class SourceClientCollection :
    public DefaultKeyedVector< audio_port_handle_t, sp<SourceClientDescriptor> >
{
public:
    void dump(String8 *dst) const;
};

typedef std::vector< sp<TrackClientDescriptor> > TrackClientVector;
typedef std::vector< sp<RecordClientDescriptor> > RecordClientVector;

// A Map that associates a portId with a client (type T)
// which is either TrackClientDescriptor or RecordClientDescriptor.

template<typename T>
class ClientMapHandler {
public:
    virtual ~ClientMapHandler() = default;

    // Track client management
    void addClient(const sp<T> &client) {
        const audio_port_handle_t portId = client->portId();
        LOG_ALWAYS_FATAL_IF(!mClients.emplace(portId, client).second,
                "%s(%d): attempting to add client that already exists", __func__, portId);
    }
    sp<T> getClient(audio_port_handle_t portId) const {
        auto it = mClients.find(portId);
        if (it == mClients.end()) return nullptr;
        return it->second;
    }
    virtual void removeClient(audio_port_handle_t portId) {
        LOG_ALWAYS_FATAL_IF(mClients.erase(portId) == 0,
                "%s(%d): client does not exist", __func__, portId);
    }
    size_t getClientCount() const {
        return mClients.size();
    }
    virtual void dump(String8 *dst) const {
        size_t index = 0;
        for (const auto& client: getClientIterable()) {
            client->dump(dst, 2, index++);
        }
    }

    // helper types
    using ClientMap = std::map<audio_port_handle_t, sp<T>>;
    using ClientMapIterator = typename ClientMap::const_iterator;  // ClientMap is const qualified
    class ClientIterable {
    public:
        explicit ClientIterable(const ClientMapHandler<T> &ref) : mClientMapHandler(ref) { }

        class iterator {
        public:
            // traits
            using iterator_category = std::forward_iterator_tag;
            using value_type = sp<T>;
            using difference_type = ptrdiff_t;
            using pointer = const sp<T>*;    // Note: const
            using reference = const sp<T>&;  // Note: const

            // implementation
            explicit iterator(const ClientMapIterator &it) : mIt(it) { }
            iterator& operator++()    /* prefix */     { ++mIt; return *this; }
            reference operator* () const               { return mIt->second; }
            reference operator->() const               { return mIt->second; } // as if sp<>
            difference_type operator-(const iterator& rhs) {return mIt - rhs.mIt; }
            bool operator==(const iterator& rhs) const { return mIt == rhs.mIt; }
            bool operator!=(const iterator& rhs) const { return mIt != rhs.mIt; }
        private:
            ClientMapIterator mIt;
        };

        iterator begin() const { return iterator{mClientMapHandler.mClients.begin()}; }
        iterator end() const { return iterator{mClientMapHandler.mClients.end()}; }

    private:
        const ClientMapHandler<T>& mClientMapHandler; // iterating does not modify map.
    };

    // return an iterable object that can be used in a range-based-for to enumerate clients.
    // this iterable does not allow modification, it should be used as a temporary.
    ClientIterable getClientIterable() const {
        return ClientIterable{*this};
    }

private:
    // ClientMap maps a portId to a client descriptor (both uniquely identify each other).
    ClientMap mClients;
};

} // namespace android
