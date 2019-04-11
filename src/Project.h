#pragma once

#include "JuceHeader.h"

namespace vcp {

class PluginManager;

class Project : public kv::ObjectModel
{
public:
    Project();
    ~Project();

    //=========================================================================
    bool writeToFile (const File&) const;
    bool loadFile (const File&);

    //=========================================================================
    bool getPluginDescription (PluginManager&, PluginDescription&) const;
    void setPluginDescription (const PluginDescription&);
    void updatePluginState (AudioProcessor& processor);
    void applyPluginState (AudioProcessor& processor) const;
    void clearPlugin();
    
    inline Project& operator= (const Project& o)
    {
        this->objectData = o.objectData;
        return *this;
    }

private:
    void setMissingProperties();
};

}