//
//  libMTSClient.cpp
//  MTS
//
//  Created by Dave on 03/06/2017.
//  Copyright © 2017 dmgaudio. All rights reserved.
//

#include "libMTSClient.h"
#include <math.h>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

const static double ratioToSemitones=12.0/log(2.0);
typedef void (*mts_pv)(void*);
typedef const double *(*mts_cd)(void);
typedef bool (*mts_bool)(void);

struct mtsclientglobal
{
    mtsclientglobal() : RegisterClient(0), DeregisterClient(0), GetTuning(0), HasMaster(0), esp_retuning(0)
    {
        for (int i=0;i<128;i++) iet[i]=1.0/(440.0*pow(2.0,(i-69.0)/12.0));
        load_lib();
        if (GetTuning) esp_retuning=GetTuning();
    }
    virtual inline bool isOnline() const {return esp_retuning && HasMaster && HasMaster();}
    
    mts_pv RegisterClient,DeregisterClient;mts_cd GetTuning;mts_bool HasMaster;    // Interface to lib
    double iet[128];const double *esp_retuning;    // tuning tables
    
#ifdef WIN
    virtual void load_lib(){
        if (!(handle=LoadLibraryA("C:\\Program Files\\MTS\\libMTS.dll"))) return;
        RegisterClient   =(mts_pv)GetProcAddress(handle,"MTS_RegisterClient");
        DeregisterClient =(mts_pv)GetProcAddress(handle,"MTS_DeregisterClient");
        GetTuning        =(mts_cd)GetProcAddress(handle,"MTS_GetTuningTable");
        HasMaster        =(mts_bool)GetProcAddress(handle,"MTS_HasMaster");
    }
    virtual ~mtsclientglobal() {FreeLibrary(handle);}
    HINSTANCE handle;
#else
    virtual void load_lib(){
        if (!(handle=dlopen("/Library/Application Support/MTS/libMTS.dylib",RTLD_NOW))) return;
        RegisterClient   =(mts_pv)dlsym(handle,"MTS_RegisterClient");
        DeregisterClient =(mts_pv)dlsym(handle,"MTS_DeregisterClient");
        GetTuning        =(mts_cd)dlsym(handle,"MTS_GetTuningTable");
        HasMaster        =(mts_bool)dlsym(handle,"MTS_HasMaster");
    }
    virtual ~mtsclientglobal() {dlclose(handle);}
    void *handle;
#endif
};

static mtsclientglobal global;

struct MTSClient
{
    MTSClient()
    {
        for (int i=0;i<128;i++) retuning[i]=440.0*pow(2.0,(i-69.0)/12.0);
        if (global.RegisterClient) global.RegisterClient((void*)this);
    }
    ~MTSClient() {if (global.DeregisterClient) global.DeregisterClient((void*)this);}
    bool hasMaster() {return global.isOnline();}
    inline double freq(int note) const
    {
        if (global.isOnline()) return global.esp_retuning[note&127];
        return retuning[note&127];
    }
    
    double retuning[128];
};

// Exported functions:
double MTS_NoteToFrequency(MTSClient* c,char midinote) {return c?c->freq(midinote):(1.0/global.iet[midinote&127]);}
double MTS_RetuningAsRatio(MTSClient* c,char midinote) {return c?c->freq(midinote)*global.iet[midinote&127]:1.0;}
double MTS_RetuningInSemitones(MTSClient* c,char midinote) {return ratioToSemitones*log(MTS_RetuningAsRatio(c,midinote));}
MTSClient* MTS_RegisterClient() {return new MTSClient;}
void MTS_DeregisterClient(MTSClient* c) {delete c;}
bool MTS_HasMaster(MTSClient* c) {return c?c->hasMaster():false;}

