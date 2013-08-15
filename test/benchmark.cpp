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
    vmanWorld world;
    vmanLayer* layers;
    int layerCount;
    int iterations;
    int maxVolumeDistance;
    int maxVolumeSize;
	float minWait;
	float maxWait;
};


// -------



std::vector<tthread::thread*> threads;

void ThreadFn( void* context )
{
    const Configuration* config = (Configuration*)context;

    vmanAccess access = vmanCreateAccess(config->world);

    for(int i = 0; i < config->iterations; ++i)
    {
        vmanVolume volume =
        {
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),

            Random(1, config->maxVolumeSize),
            Random(1, config->maxVolumeSize),
            Random(1, config->maxVolumeSize)
        };
        vmanSetAccessVolume(access, &volume);
        vmanLockAccess(access, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);

        char* baseVoxel;
        baseVoxel = (char*)vmanReadWriteVoxelLayer(access,
            Random(volume.x, volume.x + volume.w-1),
            Random(volume.y, volume.y + volume.h-1),
            Random(volume.z, volume.z + volume.d-1),
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

void PanicExit( int sig )
{
	vmanPanicExit();
}


int main( int argc, char* argv[] )
{
	signal(SIGABRT, PanicExit);
	signal(SIGFPE, PanicExit);
	signal(SIGILL, PanicExit);
	signal(SIGINT, PanicExit);
	signal(SIGSEGV, PanicExit);
	signal(SIGTERM, PanicExit);

	ReadConfigValues(argc, argv);

    srand(time(NULL));

    int layerSize = GetConfigInt("layer.size", 1);
    int layerCount = GetConfigInt("layer.count", 1);
    vmanLayer* layers = CreateLayers(layerCount, layerSize);
    int chunkEdgeLength = GetConfigInt("chunk.edge-length", 8);
    const char* worldDir = GetConfigString("world.directory", NULL);

    Configuration config;
    config.world = vmanCreateWorld(layers, layerCount, chunkEdgeLength, worldDir, true);
	vmanSetUnusedChunkTimeout(config.world, GetConfigInt("chunk.unused-timeout", 4));
	vmanSetModifiedChunkTimeout(config.world, GetConfigInt("chunk.modified-timeout", 3));

    config.layers = layers;
    config.layerCount = layerCount;
    config.iterations = GetConfigInt("thread.iterations", 100);
    config.maxVolumeDistance = GetConfigInt("thread.max-volume-distance", 10);
    config.maxVolumeSize = GetConfigInt("thread.max-volume_size", 10);
	config.minWait = GetConfigFloat("thread.min-wait", 0);
	config.maxWait = GetConfigFloat("thread.max-wait", 0);

	int threadCount = GetConfigInt("thread.count", 1);
    for(int i = 0; i < threadCount; ++i)
    {
        threads.push_back( new tthread::thread(ThreadFn, &config) );
    }

    for(int i = 0; i < threads.size(); ++i)
    {
        if(threads[i]->joinable())
            threads[i]->join();
        delete threads[i];
    }
    puts("## Benchmark threads stopped.");

    vmanDeleteWorld(config.world);
    puts("## World deleted.");

    DestroyLayers(layers, layerCount);
    puts("## Success!");

	vmanStatistics statistics;
	if(vmanGetStatistics(config.world, &statistics))
	{
		printf("chunkGetHits = %d\n", statistics.chunkGetHits);
		printf("chunkGetMisses = %d\n", statistics.chunkGetMisses);
		printf("chunkLoadOps = %d\n", statistics.chunkLoadOps);
		printf("chunkSaveOps = %d\n", statistics.chunkSaveOps);
		printf("chunkUnloadOps = %d\n", statistics.chunkUnloadOps);
		printf("volumeReadHits = %d\n", statistics.volumeReadHits);
		printf("volumeWriteHits = %d\n", statistics.volumeWriteHits);
		printf("maxLoadedChunks = %d\n", statistics.maxLoadedChunks);
		printf("maxScheduledChecks = %d\n", statistics.maxScheduledChecks);
		printf("maxEnqueuedJobs = %d\n", statistics.maxEnqueuedJobs);
	}

    return 0;
}
