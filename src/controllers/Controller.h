
#pragma once

#include "Versicap.h"

namespace vcp {

class Controller : private Versicap::Listener
{
public:
    Controller (Versicap& vc)
        : versicap (vc)
    {
        versicap.addListener (this);
    }

    virtual ~Controller()
    {
        versicap.removeListener (this);
    }

private:
    Versicap& versicap;
};

}