#pragma once

#include "JuceHeader.h"

namespace vcp {

class Versicap;

class ContentComponent : public Component
{
public:
    ContentComponent (Versicap&);
    virtual ~ContentComponent();

    virtual void stabilizeProject() =0;
    virtual void displayObject (const ValueTree&) =0;
    virtual void getState (String&) =0;
    virtual void applyState (const String&) =0;

private:
    Versicap& versicap;
};

}
