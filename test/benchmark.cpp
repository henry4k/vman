#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include <vector>
#include <map>
#include <string>
#include <tinythread.h>
#include <ini.h>
#include <vman.h>


int Random( int min, int max )
{
    return rand() % (max-min+1) + min;
}

float Random( float max )
{
	return (double(rand()) / (double(RAND_MAX) / max));
}

float Random( float min, float max )
{
	return min + Random(max-min);
}

struct Configuration
{
    vmanVolume volume;
    vmanLayer* layers;
    int layerCount;
    int iterations;
    int maxSelectionDistance;
    int maxSelectionSize;
	float minWait;
	float maxWait;
};


// -------



std::vector<tthread::thread*> threads;

void ThreadFn( void* context )
{
    const Configuration* config = (Configuration*)context;

    vmanAccess access = vmanCreateAccess(config->volume);

    for(int i = 0; i < config->iterations; ++i)
    {
        vmanSelection selection =
        {
            Random(-config->maxSelectionDistance, config->maxSelectionDistance),
            Random(-config->maxSelectionDistance, config->maxSelectionDistance),
            Random(-config->maxSelectionDistance, config->maxSelectionDistance),

            Random(1, config->maxSelectionSize),
            Random(1, config->maxSelectionSize),
            Random(1, config->maxSelectionSize)
        };
        vmanSelect(access, &selection);
        vmanLockAccess(access, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);

        char* baseVoxel;
        baseVoxel = (char*)vmanReadWriteVoxelLayer(access,
            Random(selection.x, selection.x + selection.w-1),
            Random(selection.y, selection.y + selection.h-1),
            Random(selection.z, selection.z + selection.d-1),
            Random(0, config->layerCount-1)
        );

        if(baseVoxel != NULL)
            *baseVoxel = 'X';

        vmanUnlockAccess(access);

		const float waitTime = Random(config->minWait, config->maxWait);
		if(waitTime > 0.0f)
			tthread::this_thread::sleep_for( tthread::chrono::milliseconds(waitTime*1000) );
    }

    vmanDeleteAccess(access);
}


// -----------


void CopyBytes( const void* source, void* destination, int count )
{
    memcpy(destination, source, count);
}

char* CreateString( const char* format, ... )
{
    char buf[256];

    va_list args;
    va_start(args, format);
    int len = vsprintf(buf, format, args);
    va_end(args);

    char* out = new char[len+1];
    memcpy(out, buf, len+1);
    return out;
}

vmanLayer* CreateLayers( int count, int size )
{
    vmanLayer* layers = new vmanLayer[count];
    for(int i = 0; i < count; ++i)
    {
        vmanLayer& layer = layers[i];
        layer.name = CreateString("Layer %d", i);
        layer.voxelSize = size;
        layer.revision = 1;
        layer.serializeFn = CopyBytes;
        layer.deserializeFn = CopyBytes;
    }
    return layers;
}

void DestroyLayers( vmanLayer* layers, int count )
{
    for(int i = 0; i < count; ++i)
    {
        vmanLayer& layer = layers[i];
        delete[] layer.name;
    }
    delete[] layers;
}


// -----------

std::map<std::string, std::string> g_ConfigValues;

int IniEntryCallback( void* user, const char* section, const char* name, const char* value )
{
	using namespace std;

	string key;
	if(section == NULL)
		key = string(name);
	else
		key = string(section) + string(".") + string(name);

	printf("%s = %s\n", key.c_str(), value);
	g_ConfigValues[key] = value;
	return 1;
}

void ReadConfigValues( const int argc, char** argv )
{
	for(int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		// --key=value
		
		if(
			arg.size() < 2 ||
			arg[0] != '-' ||
			arg[1] != '-'
		)
		{
			printf("Bad argument '%s'\n", arg.c_str());
			break;
		}

		const size_t equalsPos = arg.find('=');

		if(equalsPos == std::string::npos)
		{
			printf("Bad argument '%s'\n", arg.c_str());
			break;
		}

		std::string key   = arg.substr(2, equalsPos-2);
		std::string value = arg.substr(equalsPos+1);

		if(key == "config")
		{
			printf("Reading config file %s ..\n", value.c_str());
			ini_parse(value.c_str(), IniEntryCallback, NULL);
		}
		else
		{
			printf("%s = %s\n", key.c_str(), value.c_str());
			g_ConfigValues[key] = value;
		}
	}
}

const char* GetConfigString( const char* key, const char* defaultValue )
{
	std::map<std::string, std::string>::const_iterator i =
		g_ConfigValues.find(key);
	
	if(i != g_ConfigValues.end())
		return i->second.c_str();
	else
		return defaultValue;
}

int GetConfigInt( const char* key, int defaultValue )
{
	const char* str = GetConfigString(key, NULL);
	return str == NULL ? defaultValue : atoi(str);
}

float GetConfigFloat( const char* key, float defaultValue )
{
	const char* str = GetConfigString(key, NULL);
	return str == NULL ? defaultValue : atof(str);
}

// -------

void SetSignals( void (*sigfn)(int) )
{
	signal(SIGABRT, sigfn);
	signal(SIGFPE,  sigfn);
	signal(SIGILL,  sigfn);
	signal(SIGINT,  sigfn);
	signal(SIGSEGV, sigfn);
	signal(SIGTERM, sigfn);
}

void PanicExit( int sig )
{
	SetSignals(SIG_DFL);
	vmanPanicExit();
	exit(EXIT_FAILURE);
}


int main( int argc, char* argv[] )
{
	SetSignals(PanicExit);

	ReadConfigValues(argc, argv);

    srand(time(NULL));

    int layerSize = GetConfigInt("layer.size", 1);
    int layerCount = GetConfigInt("layer.count", 1);
    vmanLayer* layers = CreateLayers(layerCount, layerSize);
    int chunkEdgeLength = GetConfigInt("chunk.edge-length", 8);
    const char* volumeDir = GetConfigString("volume.directory", NULL);

    Configuration config;
    config.volume = vmanCreateVolume(layers, layerCount, chunkEdgeLength, volumeDir, true);
	vmanSetUnusedChunkTimeout(config.volume, GetConfigInt("chunk.unused-timeout", 4));
	vmanSetModifiedChunkTimeout(config.volume, GetConfigInt("chunk.modified-timeout", 3));

    config.layers = layers;
    config.layerCount = layerCount;
    config.iterations = GetConfigInt("thread.iterations", 100);
    config.maxSelectionDistance = GetConfigInt("thread.max-selection-distance", 10);
    config.maxSelectionSize = GetConfigInt("thread.max-selection_size", 10);
	config.minWait = GetConfigFloat("thread.min-wait", 0);
	config.maxWait = GetConfigFloat("thread.max-wait", 0);

	int threadCount = GetConfigInt("thread.count", 1);
    for(int i = 0; i < threadCount; ++i)
    {
		char buffer[32];
		sprintf(buffer, "Benchmarker %d", i);
        threads.push_back( new tthread::thread(ThreadFn, &config, buffer) );
    }

    for(int i = 0; i < threads.size(); ++i)
    {
        if(threads[i]->joinable())
            threads[i]->join();
        delete threads[i];
    }
    puts("## Benchmark threads stopped.");

    vmanDeleteVolume(config.volume);
    puts("## Volume deleted.");

    DestroyLayers(layers, layerCount);
    puts("## Success!");

	vmanStatistics statistics;
	if(vmanGetStatistics(config.volume, &statistics))
	{
		printf("chunkGetHits = %d\n", statistics.chunkGetHits);
		printf("chunkGetMisses = %d\n", statistics.chunkGetMisses);
		printf("chunkLoadOps = %d\n", statistics.chunkLoadOps);
		printf("chunkSaveOps = %d\n", statistics.chunkSaveOps);
		printf("chunkUnloadOps = %d\n", statistics.chunkUnloadOps);
		printf("readOps = %d\n", statistics.readOps);
		printf("writeOps = %d\n", statistics.writeOps);
		printf("maxLoadedChunks = %d\n", statistics.maxLoadedChunks);
		printf("maxScheduledChecks = %d\n", statistics.maxScheduledChecks);
		printf("maxEnqueuedJobs = %d\n", statistics.maxEnqueuedJobs);
	}

    return 0;
}
