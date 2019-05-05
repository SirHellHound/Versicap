
#pragma once

namespace kv {

class UnlockStatus : public kv::EDDOnlineUnlockStatus,
                     public ChangeBroadcaster
{
public:
    UnlockStatus() = default;
    virtual ~UnlockStatus() = default;

    inline var isExpiring() const
    {
        var result (0);
        if (getExpiryTime() > Time())
        {
            var yes (1);
            result.swapWith (yes);
        }

        return result;
    }

    virtual UnlockResult registerTrial (const String& email, const String& username, const String& password)
    {
        ignoreUnused (email, username, password);
        UnlockResult result;
        result.succeeded = false;
        result.errorMessage = "Trial registration not supported";
        return result;
    }

    virtual void loadAll() { load(); }
};

}