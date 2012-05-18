#include "common.h"
#include "input_stream.h"
#include "output_stream.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

COMMON_V8_USES

using v8::Context;
using v8::Exception;
using v8::ThrowException;
using v8::TryCatch;

////////////////////////////////////////////////////////////////////////////////

class TTestInputStream
    : public node::ObjectWrap
{
protected:
    TTestInputStream(TNodeJSInputStream* slave);
    ~TTestInputStream();

public:
    using node::ObjectWrap::Ref;
    using node::ObjectWrap::Unref;

    // Synchronous JS API.
    static Handle<Value> New(const Arguments& args);

    static Handle<Value> ReadSynchronously(const Arguments& args);

    // Asynchronous JS API.
    static Handle<Value> Read(const Arguments& args);
    static void ReadWork(uv_work_t* workRequest);
    static void ReadAfter(uv_work_t* workRequest);

private:
    // XXX(sandello): Store by intrusive ptr afterwards.
    // XXX(sandello): Investigate whether it is safe to call Ref() and Unref() on an V8 object
    // from other threads.
    TNodeJSInputStream* Slave;

    class TReadString
        : public String::ExternalAsciiStringResource
    {
    public:
        TReadString(size_t requiredLength)
            : Buffer(new char[requiredLength])
            , Length(requiredLength)
        { }

        ~TReadString()
        {
            if (Buffer) {
                delete[] Buffer;
            }
        }

        const char* data() const
        {
            return Buffer;
        }

        char* mutable_data()
        {
            return Buffer;
        }

        size_t length() const
        {
            return Length;
        }

        size_t& mutable_length()
        {
            return Length;
        }

    private:
        char*  Buffer;
        size_t Length;           
    };

    struct TReadRequest
    {
        uv_work_t Request;
        TTestInputStream* Host;

        Persistent<Function> Callback;

        char*  Buffer;
        size_t Length;

        TReadRequest(TTestInputStream* host, Handle<Integer> length, Handle<Function> callback)
            : Host(host)
            , Callback(Persistent<Function>::New(callback))
            , Length(length->Uint32Value())
        {
            THREAD_AFFINITY_IS_V8();

            if (Length) {
                Buffer = new char[Length];
            }

            YASSERT(Buffer || !Length);

            Host->Ref();
            Host->Slave->Ref();
        }

        ~TReadRequest()
        {
            THREAD_AFFINITY_IS_V8();

            if (Buffer) {
                delete[] Buffer;
            }

            Callback.Dispose();
            Callback.Clear();

            Host->Slave->Unref();
            Host->Unref();
        }
    };

private:
    TTestInputStream(const TTestInputStream&);
    TTestInputStream& operator=(const TTestInputStream&);
};

////////////////////////////////////////////////////////////////////////////////

TTestInputStream::TTestInputStream(TNodeJSInputStream* slave)
    : node::ObjectWrap()
    , Slave(slave)
{
    T_THREAD_AFFINITY_IS_V8();
}

TTestInputStream::~TTestInputStream()
{
    T_THREAD_AFFINITY_IS_V8();
}

Handle<Value> TTestInputStream::New(const Arguments& args)
{
    T_THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    YASSERT(args.Length() == 1);
    EXPECT_THAT_HAS_INSTANCE(args[0], TNodeJSInputStream);

    TTestInputStream* host = new TTestInputStream(
        ObjectWrap::Unwrap<TNodeJSInputStream>(args[0].As<Object>()));
    host->Wrap(args.This());
    return args.This();
}

////////////////////////////////////////////////////////////////////////////////

Handle<Value> TTestInputStream::ReadSynchronously(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Unwrap.
    TTestInputStream* host =
        ObjectWrap::Unwrap<TTestInputStream>(args.This());

    // Validate arguments.
    YASSERT(args.Length() == 1);
    EXPECT_THAT_IS(args[0], Uint32);

    // Do the work.
    size_t length;
    TReadString* string;

    length = args[0]->Uint32Value();
    string = new TTestInputStream::TReadString(length);
    length = host->Slave->Read(string->mutable_data(), length);
    string->mutable_length() = length;

    return scope.Close(String::NewExternal(string));
}

Handle<Value> TTestInputStream::Read(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Validate arguments.
    YASSERT(args.Length() == 2);
    EXPECT_THAT_IS(args[0], Uint32);
    EXPECT_THAT_IS(args[1], Function);

    // Do the work.
    TReadRequest* request = new TReadRequest(
        ObjectWrap::Unwrap<TTestInputStream>(args.This()),
        args[0].As<Integer>(),
        args[1].As<Function>());

    uv_queue_work(
        uv_default_loop(), &request->Request,
        TTestInputStream::ReadWork, TTestInputStream::ReadAfter);

    return Undefined();
}

void TTestInputStream::ReadWork(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_UV();
    TReadRequest* request =
        container_of(workRequest, TReadRequest, Request);

    request->Length = request->Host->Slave->Read(request->Buffer, request->Length);
}

void TTestInputStream::ReadAfter(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    TReadRequest* request =
        container_of(workRequest, TReadRequest, Request);

    {
        TryCatch block;

        Local<Value> args[] = {
            Local<Value>::New(Integer::New(request->Length)),
            Local<Value>::New(String::New(request->Buffer, request->Length))
        };

        request->Callback->Call(Context::GetCurrent()->Global(), 2, args);

        if (block.HasCaught()) {
            node::FatalException(block);
        }
    }

    delete request;
}

////////////////////////////////////////////////////////////////////////////////

class TTestOutputStream
    : public node::ObjectWrap
{
protected:
    TTestOutputStream(TNodeJSOutputStream* slave);
    ~TTestOutputStream();

public:
    using node::ObjectWrap::Ref;
    using node::ObjectWrap::Unref;

    // Synchronous JS API.
    static Handle<Value> New(const Arguments& args);

    static Handle<Value> WriteSynchronously(const Arguments& args);
    static Handle<Value> Flush(const Arguments& args);
    static Handle<Value> Finish(const Arguments& args);

    // Asynchronous JS API.
    static Handle<Value> Write(const Arguments& args);
    static void WriteWork(uv_work_t* workRequest);
    static void WriteAfter(uv_work_t* workRequest);

private:
    // XXX(sandello): See comments for TTestInputStream
    TNodeJSOutputStream* Slave;

    struct TWriteRequest
    {
        uv_work_t Request;
        TTestOutputStream* Host;

        Persistent<Function> Callback;
        String::Utf8Value ValueToBeWritten;

        char*  Buffer;
        size_t Length;

        TWriteRequest(TTestOutputStream* host, Handle<String> string, Handle<Function> callback)
            : Host(host)
            , Callback(Persistent<Function>::New(callback))
            , ValueToBeWritten(string)
        {
            THREAD_AFFINITY_IS_V8();

            Buffer = *ValueToBeWritten;
            Length = ValueToBeWritten.length();

            Host->Ref();
            Host->Slave->Ref();
        }

        ~TWriteRequest()
        {
            THREAD_AFFINITY_IS_V8();

            Callback.Dispose();
            Callback.Clear();

            Host->Slave->Unref();
            Host->Unref();
        }
    };
};

////////////////////////////////////////////////////////////////////////////////

TTestOutputStream::TTestOutputStream(TNodeJSOutputStream* slave)
    : node::ObjectWrap()
    , Slave(slave)
{
    T_THREAD_AFFINITY_IS_V8();
}

TTestOutputStream::~TTestOutputStream()
{
    T_THREAD_AFFINITY_IS_V8();
}

Handle<Value> TTestOutputStream::New(const Arguments& args)
{
    T_THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    YASSERT(args.Length() == 1);
    EXPECT_THAT_HAS_INSTANCE(args[0], TNodeJSOutputStream);

    TTestOutputStream* host = new TTestOutputStream(
        ObjectWrap::Unwrap<TNodeJSOutputStream>(args[0].As<Object>()));
    host->Wrap(args.This());
    return args.This();
}

////////////////////////////////////////////////////////////////////////////////

Handle<Value> TTestOutputStream::WriteSynchronously(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Unwrap.
    TTestOutputStream* host =
        ObjectWrap::Unwrap<TTestOutputStream>(args.This());

    // Validate arguments.
    YASSERT(args.Length() == 1);
    EXPECT_THAT_IS(args[0], String);

    // Do the work.
    String::Utf8Value value(args[0]);
    host->Slave->Write(*value, value.length());

    return Undefined();
}

Handle<Value> TTestOutputStream::Write(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Validate arguments.
    YASSERT(args.Length() == 2);
    EXPECT_THAT_IS(args[0], String);
    EXPECT_THAT_IS(args[1], Function);

    // Do the work.
    TWriteRequest* request = new TWriteRequest(
        ObjectWrap::Unwrap<TTestOutputStream>(args.This()),
        args[0].As<String>(),
        args[1].As<Function>());

    uv_queue_work(
        uv_default_loop(), &request->Request,
        TTestOutputStream::WriteWork, TTestOutputStream::WriteAfter);

    return Undefined();
}

void TTestOutputStream::WriteWork(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_UV();
    TWriteRequest* request =
        container_of(workRequest, TWriteRequest, Request);

    request->Host->Slave->Write(request->Buffer, request->Length);
}

void TTestOutputStream::WriteAfter(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    TWriteRequest* request =
        container_of(workRequest, TWriteRequest, Request);

    {
        TryCatch block;

        request->Callback->Call(Context::GetCurrent()->Global(), 0, NULL);

        if (block.HasCaught()) {
            node::FatalException(block);
        }
    }

    delete request;
}

////////////////////////////////////////////////////////////////////////////////

Handle<Value> TTestOutputStream::Flush(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Unwrap.
    TTestOutputStream* host =
        ObjectWrap::Unwrap<TTestOutputStream>(args.This());

    // Validate arguments.
    YASSERT(args.Length() == 0);

    // Do the work.
    host->Slave->Flush();

    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////

Handle<Value> TTestOutputStream::Finish(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Unwrap.
    TTestOutputStream* host =
        ObjectWrap::Unwrap<TTestOutputStream>(args.This());

    // Validate arguments.
    YASSERT(args.Length() == 0);

    // Do the work.
    host->Slave->Finish();

    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////

void ExportTestInputStream(Handle<Object> target)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    Local<FunctionTemplate> tpl = FunctionTemplate::New(TTestInputStream::New);

    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(tpl, "Read", TTestInputStream::Read);
    NODE_SET_PROTOTYPE_METHOD(tpl, "ReadSynchronously", TTestInputStream::ReadSynchronously);

    tpl->SetClassName(String::NewSymbol("TTestInputStream"));
    target->Set(String::NewSymbol("TTestInputStream"), tpl->GetFunction());
}

void ExportTestOutputStream(Handle<Object> target)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    Local<FunctionTemplate> tpl = FunctionTemplate::New(TTestOutputStream::New);

    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(tpl, "Write", TTestOutputStream::Write);
    NODE_SET_PROTOTYPE_METHOD(tpl, "WriteSynchronously", TTestOutputStream::WriteSynchronously);
    NODE_SET_PROTOTYPE_METHOD(tpl, "Flush", TTestOutputStream::Flush);
    NODE_SET_PROTOTYPE_METHOD(tpl, "Finish", TTestOutputStream::Finish);

    tpl->SetClassName(String::NewSymbol("TTestOutputStream"));
    target->Set(String::NewSymbol("TTestOutputStream"), tpl->GetFunction());
}

void ExportYTTest(Handle<Object> target)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    ExportTestInputStream(target);
    ExportTestOutputStream(target);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

NODE_MODULE(ytnode_test, NYT::ExportYTTest)
