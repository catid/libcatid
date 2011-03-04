%module(directors="1") Sphynx
%{
#include "cat\sphynx\Wrapper.hpp"
%}

%pragma(csharp) modulecode=%{

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetDllDirectory(string lpPathName);

    public static void AssemblyResolveHook()
	{
#if (DEBUG)
        SetDllDirectory(string.Format("sphynx_d{0}", (IntPtr.Size == 4) ? "32" : "64"));
#else
		SetDllDirectory(string.Format("sphynx_r{0}", (IntPtr.Size == 4) ? "32" : "64"));
#endif
    }

    [StructLayoutAttribute(LayoutKind.Sequential,Pack=1)]
    private struct RawIncomingMessage
    {
        public IntPtr data;
        public UInt32 bytes;
        public UInt32 stream;
        public UInt32 send_time;
        public byte huge_fragment; // true = part of a huge transfer, last fragment will have bytes = 0
    }

    public struct IncomingMessage
    {
        public byte[] data;
        public bool is_huge;
        public uint send_time;
        public uint stream;
    }

    public static unsafe IncomingMessage[] GetMessages(IntPtr msgs, int count)
    {
        IncomingMessage[] array = new IncomingMessage[count];

        RawIncomingMessage* ptr = (RawIncomingMessage*)msgs;

        for (int ii = 0; ii < count; ++ii)
        {
            array[ii].data = new byte[ptr[ii].bytes];

            Marshal.Copy(ptr[ii].data, array[ii].data, 0, (int)ptr[ii].bytes);

            array[ii].is_huge = ptr[ii].huge_fragment != 0;
            array[ii].send_time = ptr[ii].send_time;
            array[ii].stream= ptr[ii].stream;
        }

        return array;
    }

%}

%feature("director") EasySphynxClient;

%csmethodmodifiers WriteOOB "public unsafe";
%csmethodmodifiers WriteUnreliable "public unsafe";
%csmethodmodifiers WriteReliable "public unsafe";

%include "arrays_csharp.i"
%apply unsigned char FIXED[] {unsigned char *msg_data}

%define %cs_custom_cast(TYPE, CSTYPE)
	%typemap(ctype) TYPE, TYPE& "void*"
	%typemap(in) TYPE  %{ $1 = (TYPE)$input; %} 
	%typemap(in) TYPE& %{ $1 = (TYPE*)&$input; %} 
	%typemap(imtype, out="IntPtr") TYPE, TYPE& "CSTYPE" 
	%typemap(cstype, out="IntPtr") TYPE, TYPE& "CSTYPE" 
	%typemap(csin) TYPE, TYPE& "$csinput" 
%enddef

%cs_custom_cast(void*, IntPtr)

%include "cat\sphynx\Wrapper.hpp"
