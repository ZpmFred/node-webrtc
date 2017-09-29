/* Copyright (c) 2017 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#include "src/datachannel.h"

#include "src/common.h"

using node_webrtc::DataChannel;
using node_webrtc::DataChannelObserver;
using node_webrtc::DataChannelStateChangeEvent;
using node_webrtc::ErrorEvent;
using node_webrtc::Event;
using node_webrtc::MessageEvent;
using node_webrtc::StateEvent;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

Nan::Persistent<Function>* DataChannel::constructor;
#if NODE_MODULE_VERSION < 0x000C
Nan::Persistent<Function> DataChannel::ArrayBufferConstructor;
#endif

DataChannelObserver::DataChannelObserver(rtc::scoped_refptr<webrtc::DataChannelInterface> jingleDataChannel)
    : EventQueue() {
  TRACE_CALL;
  _jingleDataChannel = jingleDataChannel;
  _jingleDataChannel->RegisterObserver(this);
  TRACE_END;
}

DataChannelObserver::~DataChannelObserver() {
  _jingleDataChannel = nullptr;
}

void DataChannelObserver::OnStateChange() {
  TRACE_CALL;
  Enqueue(DataChannelStateChangeEvent::Create(_jingleDataChannel->state()));
  TRACE_END;
}

void DataChannelObserver::OnMessage(const webrtc::DataBuffer& buffer) {
  TRACE_CALL;
  Enqueue(MessageEvent::Create(&buffer));
  TRACE_END;
}

void requeue(DataChannelObserver& observer, DataChannel& channel) {
  while (auto event = observer.Dequeue()) {
    channel.Dispatch(std::move(event));
  }
};

DataChannel::DataChannel(node_webrtc::DataChannelObserver* observer)
    : EventLoop(*this), _binaryType(DataChannel::ARRAY_BUFFER) {
  _jingleDataChannel = observer->_jingleDataChannel;
  _jingleDataChannel->RegisterObserver(this);

  // Re-queue cached observer events
  requeue(*observer, *this);

  delete observer;
}

DataChannel::~DataChannel() {
  TRACE_CALL;
  TRACE_END;
}

NAN_METHOD(DataChannel::New) {
  TRACE_CALL;

  if (!info.IsConstructCall()) {
    return Nan::ThrowTypeError("Use the new operator to construct the DataChannel.");
  }

  auto _observer = Local<External>::Cast(info[0]);
  auto observer = static_cast<node_webrtc::DataChannelObserver*>(_observer->Value());

  auto obj = new DataChannel(observer);
  obj->Wrap(info.This());

  TRACE_END;
  info.GetReturnValue().Set(info.This());
}

void DataChannel::HandleErrorEvent(const ErrorEvent<DataChannel>& event) const {
  Nan::HandleScope scope;

  auto self = this->handle();
  auto callback = Local<Function>::Cast(self->Get(Nan::New("onerror").ToLocalChecked()));
  if (callback.IsEmpty()) {
    return;
  }

  Local<Value> argv[] = {
      Nan::Error(event.msg.c_str())
  };
  Nan::MakeCallback(self, callback, 1, argv);
}

void DataChannel::HandleStateEvent(const DataChannelStateChangeEvent& event) {
  Nan::HandleScope scope;

  if (this->_jingleDataChannel && this->_jingleDataChannel->state() == webrtc::DataChannelInterface::kClosed) {
    this->Stop();
  }

  auto self = this->handle();
  auto callback = Local<Function>::Cast(self->Get(Nan::New("onstatechange").ToLocalChecked()));
  if (callback.IsEmpty()) {
    return;
  }

  Local<Value> argv[] = {
      Nan::New(event.state)
  };
  Nan::MakeCallback(self, callback, 1, argv);
}

void DataChannel::HandleMessageEvent(const MessageEvent& event) const {
  Nan::HandleScope scope;

  auto self = this->handle();
  auto callback = Local<Function>::Cast(self->Get(Nan::New("onmessage").ToLocalChecked()));

  Local<Value> argv[1];

  if (event.binary) {
#if NODE_MODULE_VERSION > 0x000B
    auto array = v8::ArrayBuffer::New(
        v8::Isolate::GetCurrent(), event.message, event.size);
#else
    Local<Object> array = Nan::New(ArrayBufferConstructor)->NewInstance();
        array->SetIndexedPropertiesToExternalArrayData(
            event.message, v8::kExternalByteArray, event.size);
        array->ForceSet(Nan::New("byteLength").ToLocalChecked(), Nan::New<Integer>(static_cast<uint32_t>(event.size)));
#endif

    argv[0] = array;
    Nan::MakeCallback(self, callback, 1, argv);
  } else {
    auto str = Nan::New(event.message, static_cast<int>(event.size)).ToLocalChecked();

    // cleanup message event
    delete[] event.message;

    argv[0] = str;
    Nan::MakeCallback(self, callback, 1, argv);
  }
}

void DataChannel::DidStop() {
  this->_jingleDataChannel->UnregisterObserver();
  this->_jingleDataChannel = nullptr;
}

void DataChannel::OnStateChange() {
  TRACE_CALL;
  Dispatch(DataChannelStateChangeEvent::Create(_jingleDataChannel->state()));
  TRACE_END;
}

void DataChannel::OnMessage(const webrtc::DataBuffer& buffer) {
  TRACE_CALL;
  Dispatch(MessageEvent::Create(&buffer));
  TRACE_END;
}

NAN_METHOD(DataChannel::Send) {
  TRACE_CALL;

  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.This());

  if (info[0]->IsString()) {
    webrtc::DataBuffer buffer(*String::Utf8Value(Local<String>::Cast(info[0])));
    self->_jingleDataChannel->Send(buffer);
  } else {
#if NODE_MINOR_VERSION >= 11 || NODE_MAJOR_VERSION > 0
    Local<v8::ArrayBuffer> arraybuffer;

    if (info[0]->IsArrayBuffer()) {
      arraybuffer = Local<v8::ArrayBuffer>::Cast(info[0]);
    } else {
      arraybuffer = Local<v8::ArrayBufferView>::Cast(info[0])->Buffer();
    }

    auto content = arraybuffer->Externalize();
    rtc::Buffer buffer(static_cast<char*>(content.Data()), content.ByteLength());

#else
    Local<Object> arraybuffer = Local<Object>::Cast(info[0]);
    void* data = arraybuffer->GetIndexedPropertiesExternalArrayData();
    uint32_t data_len = arraybuffer->GetIndexedPropertiesExternalArrayDataLength();

    rtc::Buffer buffer(data, data_len);

#endif

    self->_jingleDataChannel->Send(webrtc::DataBuffer(buffer, true));

#if NODE_MINOR_VERSION >= 11 || NODE_MAJOR_VERSION > 0
    arraybuffer->Neuter();
#endif
  }

  TRACE_END;
}

NAN_METHOD(DataChannel::Close) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.This());
  self->_jingleDataChannel->Close();

  TRACE_END;
}

NAN_METHOD(DataChannel::Shutdown) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.This());
  self->Stop();

  TRACE_END;
}

NAN_GETTER(DataChannel::GetBufferedAmount) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.Holder());
  auto buffered_amount = self->_jingleDataChannel->buffered_amount();

  TRACE_END;
  info.GetReturnValue().Set(Nan::New(static_cast<uint32_t>(buffered_amount)));
}

NAN_GETTER(DataChannel::GetLabel) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.Holder());
  auto label = self->_jingleDataChannel->label();

  TRACE_END;
  info.GetReturnValue().Set(Nan::New(label).ToLocalChecked());
}

NAN_GETTER(DataChannel::GetReadyState) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.Holder());
  auto state = self->_jingleDataChannel->state();

  TRACE_END;
  info.GetReturnValue().Set(Nan::New(state));
}

NAN_GETTER(DataChannel::GetBinaryType) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.Holder());

  TRACE_END;
  info.GetReturnValue().Set(Nan::New(self->_binaryType));
}

NAN_SETTER(DataChannel::SetBinaryType) {
  TRACE_CALL;
  auto self = Nan::ObjectWrap::Unwrap<DataChannel>(info.Holder());
  self->_binaryType = static_cast<BinaryType>(value->Uint32Value());

  TRACE_END;
}

NAN_SETTER(DataChannel::ReadOnly) {
  INFO("PeerConnection::ReadOnly");
}

void DataChannel::Init(Handle<Object> exports) {
  auto tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("DataChannel").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "close", Close);
  Nan::SetPrototypeMethod(tpl, "shutdown", Shutdown);
  Nan::SetPrototypeMethod(tpl, "send", Send);

  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("bufferedAmount").ToLocalChecked(), GetBufferedAmount, ReadOnly);
  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("label").ToLocalChecked(), GetLabel, ReadOnly);
  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("binaryType").ToLocalChecked(), GetBinaryType, SetBinaryType);
  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("readyState").ToLocalChecked(), GetReadyState, ReadOnly);

  constructor = new Nan::Persistent<Function>(tpl->GetFunction());
  exports->Set(Nan::New("DataChannel").ToLocalChecked(), tpl->GetFunction());

#if NODE_MODULE_VERSION < 0x000C
  Local<Object> global = Nan::GetCurrentContext()->Global();
  Local<Value> obj = global->Get(Nan::New("ArrayBuffer").ToLocalChecked());
  ArrayBufferConstructor.Reset(obj.As<Function>());
#endif
}

void DataChannel::Dispose() {
  delete constructor;
}
