#pragma once

#include "JuceHeader.h"

namespace vcp {

struct FormatType
{
    enum ID
    {
        WAVE,
        AIFF
    };

    enum
    {
        NumTypes = 2,
        Begin = 0,
        End = NumTypes
    };

    static StringArray getChoices()
    {
        StringArray choices;
        for (int i = Begin; i < End; ++i)
            choices.add (getName (i));
        return choices;
    }

    static Array<var> getValues()
    {
        Array<var> values;
        for (int i = Begin; i < End; ++i)
            values.add (getSlug (i));
        return values;
    }

    static String getName (int t)
    {
        switch (t)
        {
            case WAVE: return "WAVE"; break;
            case AIFF: return "AIFF"; break;
        }

        jassertfalse;
        return "None";
    }

    static String getSlug (int t)
    {
        switch (t)
        {
            case WAVE: return "wave"; break;
            case AIFF: return "aiff"; break;
        }

        jassertfalse;
        return "none";
    }

    static int fromSlug (const String& t)
    {
        if (t == "wave" || t == "wav")  return WAVE;
        if (t == "aiff" || t == "aif")  return AIFF;
        
        jassertfalse;
        return -1;
    }

    static String getFileExtension (int t)
    {
        switch (t)
        {
            case WAVE: return "wav"; break;
            case AIFF: return "aiff"; break;
        }

        jassertfalse;
        return {};
    }
};

struct LoopType
{
    enum ID
    {
        None = 0,
        Forwards,
        Alternating,
        Reverse,
        RoundRobin
    };
    
    enum
    {
        NumTypes = 5,
        Begin = 0,
        End = NumTypes
    };

    inline String getName() const { return getName (type); }
    inline static String getName (int t)
    {
        switch (t)
        {
            case None:          return "None"; break;
            case Forwards:      return "Forwards"; break;
            case Alternating:   return "Alertnating"; break;
            case Reverse:       return "Reverse"; break;
            case RoundRobin:    return "Round Robin"; break;
        }
        return "Unknown";
    }

    inline String getSlug() const { return getSlug (type); }
    inline static String getSlug (int t)
    {
        switch (t)
        {
            case None:          return "none"; break;
            case Forwards:      return "forwards"; break;
            case Alternating:   return "alertnating"; break;
            case Reverse:       return "reverse"; break;
            case RoundRobin:    return "roundRobin"; break;
        }

        return "Unknown";
    }

    LoopType() = default;
    LoopType (const int t) : type (t) { jassert (isPositiveAndBelow (t, NumTypes)); }
    LoopType (const ID t) : type (static_cast<int> (t)) {}
    LoopType (const LoopType& o) { operator= (o); }
    LoopType& operator= (const LoopType& o) { type = o.type; return *this; }
    LoopType& operator= (const int t) { type = t; jassert (isPositiveAndBelow (t, NumTypes)); return *this; }
    LoopType& operator= (const ID t) { type = static_cast<int> (t); return *this; }

    bool operator== (const int t) const { return t == type; }
    bool operator!= (const int t) const { return t != type; }
    bool operator== (const ID t)  const { return t == type; }
    bool operator!= (const ID t)  const { return t != type; }

private:
    int type = 0;
};

struct SourceType
{
    enum ID
    {
        MidiDevice  = 0,
        AudioPlugin
    };

    enum
    {
        NumTypes = 2,
        Begin = 0,
        End = NumTypes
    };

    SourceType() : type (MidiDevice) { }
    SourceType (const int t)            { operator= (t); }
    SourceType (const ID t)             { operator= (t); }
    SourceType (const SourceType& o)    { operator= (o); }

    static StringArray getChoices()
    {
        StringArray names;
        for (int i = Begin; i < End; ++i)
            names.add (getName (i));
        return names;
    }

    static Array<var> getValues()
    {
        Array<var> values;
        for (int i = Begin; i < End; ++i)
            values.add (getSlug (i));
        return values;
    }

    String getName() const { return getName (type); }
    static String getName (int t)
    {
        switch (t)
        {
            case MidiDevice: return "MIDI Device"; break;
            case AudioPlugin: return "Audio Plugin"; break;
        }

        return "none";
    }

    String getSlug() const { return getSlug (type); }
    static String getSlug (int t)
    {
        switch (t)
        {
            case MidiDevice: return "midi"; break;
            case AudioPlugin: return "plugin"; break;
        }

        return "none";
    }

    static int fromSlug (const String& t)
    {
        if (t == "midi")    return MidiDevice;
        if (t == "plugin")  return AudioPlugin;
        jassertfalse;
        return -1;
    }

    operator int() const                            { return type; }
    operator var() const                            { return var (type); }
    SourceType& operator= (const SourceType& o)     { type = o.type; return *this; }
    SourceType& operator= (const SourceType::ID t)  { type = t; return *this; }
    SourceType& operator= (const int t)             { type = jlimit ((int) Begin, (int) End - 1, t); return *this; }
    bool operator== (const SourceType& o) const     { return type == o.type; }
    bool operator== (const int t) const             { return type == t; }
    bool operator== (const SourceType::ID t) const  { return type == static_cast<int> (t); }
    bool operator!= (const SourceType& o) const     { return type != o.type; }
    bool operator!= (const int t) const             { return type != t; }
    bool operator!= (const SourceType::ID t) const  { return type != static_cast<int> (t); }

private:
    int type;
};

}
