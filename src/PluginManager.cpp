/*
    PluginManager.cpp - This file is part of Element
    Copyright (C) 2014-2018  Kushview, LLC.  All rights reserved.
*/

#include "Versicap.h"
#include "PluginManager.h"
#include "Settings.h"

#define VCP_DEAD_AUDIO_PLUGINS_FILENAME          "DeadAudioPlugins.txt"
#define VCP_PLUGIN_SCANNER_SLAVE_LIST_PATH       "Temp/SlavePluginList.xml"
#define VCP_PLUGIN_SCANNER_WAITING_STATE         "waiting"
#define VCP_PLUGIN_SCANNER_READY_STATE           "ready"

#define VCP_PLUGIN_SCANNER_READY_ID              "ready"
#define VCP_PLUGIN_SCANNER_START_ID              "start"
#define VCP_PLUGIN_SCANNER_FINISHED_ID           "finished"

#define VCP_PLUGIN_SCANNER_DEFAULT_TIMEOUT       20000  // 20 Seconds

namespace vcp {

/* noop. prevent OS error dialogs from child process */ 
static void pluginScannerSlaveCrashHandler (void*) { }

class PluginScannerMaster : public kv::ChildProcessMaster,
                            public AsyncUpdater
{
public:
    explicit PluginScannerMaster (PluginScanner& o) : owner(o) { }
    ~PluginScannerMaster() { }
    
    bool startScanning (const StringArray& names = StringArray())
    {
        if (isRunning())
            return true;
        
        {
            ScopedLock sl (lock);
            slaveState  = "waiting";
            running     = false;
            formatNames = names;
        }
        
        const bool res = launchScanner();
        
        {
            ScopedLock sl (lock);
            running = res;
        }
        
        return res;
    }
    
    void handleMessageFromSlave (const MemoryBlock& mb) override
    {
        const auto data (mb.toString());
        const auto type (data.upToFirstOccurrenceOf (":", false, false));
        const auto message (data.fromFirstOccurrenceOf (":", false, false));
        
        if (type == "state")
        {
            ScopedLock sl (lock);
            const String lastState = slaveState;
            slaveState = message;
            if (lastState != slaveState)
            {
                ScopedUnlock sul (lock);
                triggerAsyncUpdate();
            }
        }
        else if (type == "name")
        {
            owner.listeners.call (&PluginScanner::Listener::audioPluginScanStarted, message.trim());
            ScopedLock sl (lock);
            pluginBeingScanned = message.trim();
        }
        else if (type == "progress")
        {
            float newProgress = (float) var (message);
            owner.listeners.call (&PluginScanner::Listener::audioPluginScanProgress, newProgress);
            ScopedLock sl (lock);
            progress = newProgress;
        }
    }
    
    void handleConnectionLost() override
    {
        // this probably will happen when a plugin crashes.
        {
            ScopedLock sl (lock);
            running = false;
        }
        
        triggerAsyncUpdate();
    }
    
    void handleAsyncUpdate() override
    {
        const auto state = getSlaveState();
        if (state == "ready" && isRunning())
        {
            String msg = "scan:"; msg << formatNames.joinIntoString(",");
            MemoryBlock mb (msg.toRawUTF8(), msg.length());
            sendMessageToSlave (mb);
        }
        else if (state == "scanning")
        {
            if (! isRunning())
            {
                DBG("[VCP] a plugin crashed or timed out during scan");
                updateListAndLaunchSlave();
            }
            else
            {
                DBG("[VCP] scanning... and running....");
            }
        }
		else if (state == "finished")
		{
			DBG("[VCP] slave finished scanning");
			{
				ScopedLock sl(lock);
				running = false;
				slaveState = "idle";
			}
            owner.listeners.call (&PluginScanner::Listener::audioPluginScanFinished);
        }
        else if (state == "waiting")
        {
            if (! isRunning())
            {
                DBG("[VCP] waiting for plugin scanner");
                updateListAndLaunchSlave();
            }
        }
		else if (slaveState == "quitting")
		{
			return;
		}
        else
        {
            DBG("[VCP] invalid slave state: " << state);
        }
    }
    
    const String getSlaveState() const
    {
        ScopedLock sl (lock);
        return slaveState;
    }
    
    float getProgress() const
    {
        ScopedLock sl (lock);
        return progress;
    }
    
    bool isRunning() const
    {
        ScopedLock sl (lock);
        return running;
    }
    
    bool sendQuitMessage()
    {
        if (isRunning())
            return sendMessageToSlave (MemoryBlock ("quit", 4));
        return false;
    }
    
private:
    PluginScanner& owner;

    CriticalSection lock;
    bool running    = false;
    float progress  = 0.f;
    String slaveState;
    StringArray formatNames;
    StringArray faileFiles;
    
    String pluginBeingScanned;

    void updateListAndLaunchSlave()
    {
        if (ScopedPointer<XmlElement> xml = XmlDocument::parse (PluginScanner::getSlavePluginListFile()))
            owner.list.recreateFromXml (*xml);
        
        const bool res = launchScanner();
        ScopedLock sl (lock);
        running = res;
    }
    
    void resetScannerVariables()
    {
        ScopedLock sl (lock);
        pluginBeingScanned = String();
        progress = -1.f;
    }
    
    bool launchScanner (const int timeout = VCP_PLUGIN_SCANNER_DEFAULT_TIMEOUT, const int flags = 0)
    {
        resetScannerVariables();
        return launchSlaveProcess (File::getSpecialLocation (File::invokedExecutableFile),
                                   VCP_PLUGIN_SCANNER_PROCESS_ID, timeout, flags);
    }
};

class PluginScannerSlave : public kv::ChildProcessSlave, public AsyncUpdater
{
public:
    PluginScannerSlave()
    {
        scanFile = PluginScanner::getSlavePluginListFile();
        SystemStats::setApplicationCrashHandler (pluginScannerSlaveCrashHandler);
    }
    
    ~PluginScannerSlave() { }
    
    void handleMessageFromMaster (const MemoryBlock& mb) override
    {
        const auto data (mb.toString());
        const auto type (data.upToFirstOccurrenceOf (":", false, false));
        const auto message (data.fromFirstOccurrenceOf (":", false, false));
        
        if (type == "quit")
        {
            handleConnectionLost();
            return;
        }
        
        if (type == "scan")
        {
            const auto formats (StringArray::fromTokens (message.trim(), ",", "'"));
            formatsToScan = formats;
            triggerAsyncUpdate();
        }
    }
    
    void handleAsyncUpdate() override
    {
        if (! scanFile.existsAsFile())
        {
            sendState ("scanning");
            sendState ("finished");
            return;
        }
        
        updateScanFileWithSettings();
        
        sendState ("scanning");
        
        for (const auto& format : formatsToScan)
            scanFor (format);
        
        settings->saveIfNeeded();
        sendState ("finished");
    }
    
    void updateScanFileWithSettings()
    {
        if (! plugins)
            return;
        
        for (int i = 0; i < plugins->getKnownPlugins().getNumTypes(); ++i)
            if (auto* type = plugins->getKnownPlugins().getType (i))
                pluginList.addType (*type);
        
        for (const auto& file : plugins->getKnownPlugins().getBlacklistedFiles())
            pluginList.addToBlacklist (file);
        
        writePluginListNow();
    }
    
    void handleConnectionMade() override
    {
        settings    = new Settings();
        plugins     = new PluginManager();
        
        if (! scanFile.existsAsFile())
            scanFile.create();
        
        if (ScopedPointer<XmlElement> xml = XmlDocument::parse (scanFile))
            pluginList.recreateFromXml (*xml);
        
        // This must happen before user settings, PluginManager will delete the deadman file
        // when restoring user plugins
        PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (
            pluginList, plugins->getDeadAudioPluginsFile());
        
        plugins->addDefaultFormats();
        
        auto pluginsFile = Versicap::getApplicationDataPath().getChildFile ("plugins.xml");
        if (auto xml = std::unique_ptr<XmlElement> (XmlDocument::parse (pluginsFile)))
            plugins->restoreUserPlugins (*xml);
        
        sendState (VCP_PLUGIN_SCANNER_READY_ID);
    }
    
    void handleConnectionLost() override
    {
        settings    = nullptr;
        plugins     = nullptr;
        scanner     = nullptr;
        exit (0);
    }

private:
    ScopedPointer<Settings> settings;
    ScopedPointer<PluginManager> plugins;
    ScopedPointer<PluginDirectoryScanner> scanner;
    String fileOrIdentifier;
    KnownPluginList pluginList;
    StringArray filesToSkip;
    File scanFile;
    StringArray formatsToScan;
    
    void applyDeadPlugins()
    {
        PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (
            pluginList, plugins->getDeadAudioPluginsFile());
    }
    
    bool writePluginListNow()
    {
        applyDeadPlugins();
        if (ScopedPointer<XmlElement> xml = pluginList.createXml())
            return xml->writeToFile (scanFile, String());
        return false;
    }
    
    bool sendState (const String& state)
    {
        return sendString ("state", state);
    }
    
    bool sendString (const String& type, const String& message)
    {
		String data = type; data << ":" << message.trim();
		MemoryBlock mb (data.toRawUTF8(), data.getNumBytesAsUTF8());
        return sendMessageToMaster (mb);
    }
    
    bool doNextScan()
    {
        const auto nextFile = scanner->getNextPluginFileThatWillBeScanned();
        sendString ("name", nextFile);
        for (const auto& file : scanner->getFailedFiles())
            pluginList.addToBlacklist (file);

        if (scanner->scanNextFile (true, fileOrIdentifier))
        {
            writePluginListNow();
            return true;
        }
        
        return false;
    }
    
    void scanFor (const String& formatName)
    {
        if (plugins == nullptr || settings == nullptr)
            return;
        if (auto* format = plugins->getAudioPluginFormat (formatName))
            scanFor (*format);
    }
    
    void scanFor (AudioPluginFormat& format)
    {
        if (plugins == nullptr || settings == nullptr)
            return;
        
       #if 0
        const auto key = String(settings->lastPluginScanPathPrefix) + format.getName();
        FileSearchPath path (settings->getUserSettings()->getValue (key));
       #else
        FileSearchPath path = format.getDefaultLocationsToSearch();
       #endif
        scanner = new PluginDirectoryScanner (pluginList, format, path, true,
                                              plugins->getDeadAudioPluginsFile(),
                                              false);
        
        while (doNextScan())
            sendString ("progress", String (scanner->getProgress()));
        
        writePluginListNow();
    }
};

// MARK: Plugin Scanner

PluginScanner::PluginScanner (KnownPluginList& listToManage) : list(listToManage) { }
PluginScanner::~PluginScanner()
{
    listeners.clear();
    master = nullptr;
}

void PluginScanner::cancel()
{
    if (master)
    {
        master->cancelPendingUpdate();
        master->sendQuitMessage();
		master = nullptr;
    }
}

bool PluginScanner::isScanning() const { return master && master->isRunning(); }

void PluginScanner::scanForAudioPlugins (const juce::String &formatName)
{
    scanForAudioPlugins (StringArray ({ formatName }));
}

void PluginScanner::scanForAudioPlugins (const StringArray& formats)
{
    cancel();
    getSlavePluginListFile().deleteFile();
	if (master == nullptr)
		master = new PluginScannerMaster (*this);
	if (master->isRunning())
		return;
    master->startScanning (formats);
}

void PluginScanner::timerCallback()
{
}

// MARK: Unverified Plugins

typedef HashMap<String, StringArray> UnverifiedPluginMap;
typedef HashMap<String, FileSearchPath> UnverifiedPluginPaths;

class UnverifiedPlugins : private Thread
{
public:
    UnverifiedPlugins() : Thread ("vcpuvpl") { }

    ~UnverifiedPlugins()
    {
        cancelFlag.set (1);
        if (isThreadRunning())
            stopThread (1000);
    }

    void searchForPlugins (PropertiesFile* props)
    {
        if (isThreadRunning())
            return;

        if (props)
        {
            const StringArray formats = { "AU", "VST", "VST3" };
            for (const auto& f : formats)
            {
                const auto key = String ("lastPluginScanPathPrefix") + f;
                paths.set (f, FileSearchPath (props->getValue (key)));
            }
        }
        else
        {
            paths.clear();
        }

        startThread (4);
    }

    void getPlugins (OwnedArray<PluginDescription>& plugs, 
                     const String& format, KnownPluginList& list)
    {
        ScopedLock sl (lock);
        if (plugins.contains (format))
        {
            for (const auto& file : plugins.getReference (format))
            {
                if (nullptr != list.getTypeForFile (file))
                    continue;
                auto* const desc = plugs.add (new PluginDescription());
                desc->pluginFormatName = format;
                desc->fileOrIdentifier = file;
            }
        }
    }

private:
    friend class Thread;
    CriticalSection lock;
    UnverifiedPluginMap plugins;
    UnverifiedPluginPaths paths;
    Atomic<int> cancelFlag;

    void run() override
    {
        cancelFlag.set (0);

        PluginManager pluginManager;
        pluginManager.addDefaultFormats();
        auto& manager (pluginManager.getAudioPluginFormats());

        for (int i = 0; i < manager.getNumFormats(); ++i)
        {
            if (threadShouldExit() || cancelFlag.get() != 0)
                break;
            
            auto* const format = manager.getFormat (i);
            FileSearchPath path = paths [format->getName()];
            path.addPath (format->getDefaultLocationsToSearch());
            const auto found = format->searchPathsForPlugins (path, true, false);

            ScopedLock sl (lock);
            plugins.set (format->getName(), found);
        }

        cancelFlag.set (0);
    }
};

// MARK: Plugin Manager
    
class PluginManager::Private : public PluginScanner::Listener
{
public:
	Private (PluginManager& o)
        : owner(o)
	{
		deadAudioPlugins = Versicap::getApplicationDataPath()
            .getChildFile (VCP_DEAD_AUDIO_PLUGINS_FILENAME);
	}

	~Private() {  }

	/** returns true if anything changed in the plugin list */
	bool updateBlacklistedAudioPlugins()
	{
		bool didSomething = false;

		if (deadAudioPlugins.existsAsFile())
		{
			PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(
				allPlugins, deadAudioPlugins);
			deadAudioPlugins.deleteFile();
			didSomething = true;
		}

		return didSomething;
	}

    void searchUnverifiedPlugins (PropertiesFile* props)
    {
        unverified.searchForPlugins (props);
    }

    void getUnverifiedPlugins (const String& format, OwnedArray<PluginDescription>& plugs)
    {
        unverified.getPlugins (plugs, format, allPlugins);
    }

private:
	friend class PluginManager;
	PluginManager& owner;
	AudioPluginFormatManager formats;
	KnownPluginList allPlugins;
	File deadAudioPlugins;
    UnverifiedPlugins unverified;
	double sampleRate = 44100.0;
	int    blockSize = 512;
	ScopedPointer<PluginScanner> scanner;
   #if ELEMENT_LV2_PLUGIN_HOST
	OptionalPtr<LV2World> lv2;
	OptionalPtr<SymbolMap> symbols;
   #endif

	void scanAudioPlugins (const StringArray& names)
	{
		if (scanner)
		{
			scanner->removeListener (this);
			scanner->cancel();
			scanner = nullptr;
		}

		StringArray formatsToScan = names;
		if (formatsToScan.isEmpty())
			for (int i = 0; i < formats.getNumFormats(); ++i)
				if (formats.getFormat(i)->canScanForPlugins())
					formatsToScan.add (formats.getFormat(i)->getName());

		scanner = new PluginScanner (allPlugins);
		scanner->addListener (this);
		scanner->scanForAudioPlugins (formatsToScan);
	}

	void audioPluginScanFinished() override
	{
		{
			ScopedLock sl(lock);
			scannedPlugin = String();
			progress = -1.0;
		}

		owner.scanFinished();
	}

	void audioPluginScanStarted (const String& plugin) override
	{
		ScopedLock sl (lock);
		scannedPlugin = plugin;
	}

	void audioPluginScanProgress (const float p) override
	{
		ScopedLock sl (lock);
		progress = p;
	}

	String getScannedPluginName() const
	{
		ScopedLock sl (lock);
		return scannedPlugin;
	}

private:
	CriticalSection lock;
	String scannedPlugin;
	float progress = -1.0;
};

PluginManager::PluginManager()
{
    priv = new Private (*this);
}

PluginManager::~PluginManager()
{
    priv = nullptr;
}

void PluginManager::addDefaultFormats()
{
    getAudioPluginFormats().addDefaultFormats();
   #if HAVE_LILV && HAVE_SUIL
    addFormat (new jlv2::LV2PluginFormat());
   #endif
}

void PluginManager::addFormat (AudioPluginFormat* fmt)
{
    getAudioPluginFormats().addFormat (fmt);
}

void PluginManager::addToKnownPlugins (const PluginDescription& desc)
{
    auto* const format = getAudioPluginFormat (desc.pluginFormatName);
    auto& list = priv->allPlugins;
    if (format && nullptr == list.getTypeForFile (desc.fileOrIdentifier))
    {
        OwnedArray<PluginDescription> dummy;
        list.removeFromBlacklist (desc.fileOrIdentifier);
        list.scanAndAddFile (desc.fileOrIdentifier, true, dummy, *format);
    }
}

void PluginManager::searchUnverifiedPlugins()
{
    if (! priv) return;
    priv->searchUnverifiedPlugins (this->props);
}

kv::ChildProcessSlave* PluginManager::createAudioPluginScannerSlave()
{
    return new PluginScannerSlave();
}

PluginScanner* PluginManager::createAudioPluginScanner()
{
    auto* scanner = new PluginScanner (getKnownPlugins());
    return scanner;
}

PluginScanner* PluginManager::getBackgroundAudioPluginScanner()
{
	if (!priv) return 0;
	if (!priv->scanner) {
		priv->scanner = createAudioPluginScanner();
		priv->scanner->addListener (priv.get());
	}
	return priv->scanner.get();
}

bool PluginManager::isScanningAudioPlugins()
{
    return (priv && priv->scanner) ? priv->scanner->isScanning()
                                   : false;
}

AudioPluginInstance* PluginManager::createAudioPlugin (const PluginDescription& desc, String& errorMsg)
{
    return getAudioPluginFormats().createPluginInstance (
        desc, priv->sampleRate, priv->blockSize, errorMsg);
}

AudioPluginFormatManager& PluginManager::getAudioPluginFormats()
{
    return priv->formats;
}

AudioPluginFormat* PluginManager::getAudioPluginFormat (const String& name) const
{
    auto& manager = priv->formats;
    for (int i = 0; i < manager.getNumFormats(); ++i)
    {
        AudioPluginFormat* fmt = manager.getFormat (i);
        if (fmt && fmt->getName() == name)
            return fmt;
    }

    return nullptr;
}

KnownPluginList& PluginManager::getKnownPlugins() { return priv->allPlugins; }
const KnownPluginList& PluginManager::getKnownPlugins() const { return priv->allPlugins; }
const File& PluginManager::getDeadAudioPluginsFile() const { return priv->deadAudioPlugins; }

void PluginManager::restoreUserPlugins (const XmlElement& xml)
{
	priv->allPlugins.recreateFromXml (xml);
    priv->updateBlacklistedAudioPlugins();
}

void PluginManager::setPlayConfig (double sampleRate, int blockSize)
{
    priv->sampleRate = sampleRate;
    priv->blockSize  = blockSize;
}

void PluginManager::scanAudioPlugins (const StringArray& names)
{
    if (! priv)
        return;

    if (isScanningAudioPlugins())
        return;
    
    priv->scanAudioPlugins (names);
}

String PluginManager::getCurrentlyScannedPluginName() const
{
	return (priv) ? priv->getScannedPluginName() : String();
}

void PluginManager::getUnverifiedPlugins (const String& formatName, OwnedArray<PluginDescription>& plugins)
{
    priv->getUnverifiedPlugins (formatName, plugins);
    if (plugins.isEmpty())
        priv->searchUnverifiedPlugins (props);
}

void PluginManager::scanFinished()
{
    restoreAudioPlugins (PluginScanner::getSlavePluginListFile());
    if (auto* scanner = getBackgroundAudioPluginScanner())
        scanner->cancel();
    jassert(! isScanningAudioPlugins());
    sendChangeMessage();
}

void PluginManager::restoreAudioPlugins (const File& file)
{
    if (ScopedPointer<XmlElement> xml = XmlDocument::parse (file))
        restoreUserPlugins (*xml);
}

const File& PluginScanner::getSlavePluginListFile()
{
    static File _listTempFile;

    if (_listTempFile == File())
    {
        _listTempFile = Versicap::getApplicationDataPath()
            .getChildFile (VCP_PLUGIN_SCANNER_SLAVE_LIST_PATH);
    }

    return _listTempFile;
}

}
