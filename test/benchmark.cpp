#include <assert.h>
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

	float secondsPerStatisticSample;
	std::string statisticsFile;
};


// -------



std::vector<tthread::thread*> threads;

void BenchmarkerThread( void* context )
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

std::string GetConfigString( const char* key, const char* defaultValue )
{
	const std::map<std::string, std::string>::const_iterator i =
		g_ConfigValues.find(key);
	
	if(i != g_ConfigValues.end())
		return i->second;
	else
		return defaultValue;
}

int GetConfigInt( const char* key, int defaultValue )
{
	const std::string str = GetConfigString(key, "");
	return str.empty() ? defaultValue : atoi(str.c_str());
}

float GetConfigFloat( const char* key, float defaultValue )
{
	const std::string str = GetConfigString(key, "");
	return str.empty() ? defaultValue : atof(str.c_str());
}

bool GetConfigBool( const char* key, bool defaultValue )
{
	const std::string str = GetConfigString(key, "");
	switch(str[0])
	{
		case '0':
		case 'f':
		case 'F':
			return false;

		case '1':
		case 't':
		case 'T':
			return true;

		default:
			return defaultValue;
	}
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

// ----------

bool                        g_StopStatistics;
tthread::mutex              g_StopStatisticsMutex;
tthread::condition_variable g_StopStatisticsCV;

void WriteStatistics( const Configuration* config, FILE* file, time_t startTime )
{
	vmanStatistics statistics;
	if(vmanGetStatistics(config->volume, &statistics) == false)
		assert(false);

	fprintf(file,
		"%9.4f %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
		difftime(time(NULL), startTime),
		statistics.chunkGetHits,
		statistics.chunkGetMisses,
		statistics.chunkLoadOps,
		statistics.chunkSaveOps,
		statistics.chunkUnloadOps,
		statistics.readOps,
		statistics.writeOps,
		statistics.maxLoadedChunks,
		statistics.maxScheduledChecks,
		statistics.maxEnqueuedJobs
	);

	vmanResetStatistics(config->volume);
}

void StatisticsWriterThread( void* context )
{
	tthread::lock_guard<tthread::mutex> guard(g_StopStatisticsMutex);
    const Configuration* config = (Configuration*)context;

	const time_t startTime = time(NULL);
	FILE* statisticsFile;
	if(config->statisticsFile.empty())
	{
		statisticsFile = stdout;
	}
	else
	{
		statisticsFile = fopen(config->statisticsFile.c_str(), "w");
		fprintf(statisticsFile,
			"# time "
			"chunkGetHits "
			"chunkGetMisses "
			"chunkLoadOps "
			"chunkSaveOps "
			"chunkUnloadOps "
			"readOps "
			"writeOps "
			"maxLoadedChunks "
			"maxScheduledChecks "
			"maxEnqueuedJobs\n"
		);
	}

	if(config->secondsPerStatisticSample > 0.0f)
	{
		while(true)
		{
			 WriteStatistics(config, statisticsFile, startTime);

			 g_StopStatisticsCV.wait_for(
				 g_StopStatisticsMutex,
				 tthread::chrono::milliseconds( config->secondsPerStatisticSample*1000 )
			 );

			 if(g_StopStatistics)
				 break;
		}
	}
	else
	{
		while(g_StopStatistics == false)
			g_StopStatisticsCV.wait(g_StopStatisticsMutex);
	}

	WriteStatistics(config, statisticsFile, startTime);

	if(statisticsFile != stdout)
		fclose(statisticsFile);
}



// ----------

int main( int argc, char* argv[] )
{
	SetSignals(PanicExit);

	ReadConfigValues(argc, argv);

    srand(time(NULL));

    const int layerSize = GetConfigInt("layer.size", 1);
    const int layerCount = GetConfigInt("layer.count", 1);
    vmanLayer* layers = CreateLayers(layerCount, layerSize);
    const int chunkEdgeLength = GetConfigInt("chunk.edge-length", 8);
    const std::string volumeDir = GetConfigString("volume.directory", "");

    Configuration config;

	vmanVolumeParameters volumeParams;
	vmanInitVolumeParameters(&volumeParams);
	volumeParams.layers = layers;
	volumeParams.layerCount = layerCount;
	volumeParams.chunkEdgeLength = chunkEdgeLength;
	volumeParams.baseDir = volumeDir.empty() ? NULL : volumeDir.c_str();
	volumeParams.enableStatistics = true;
    config.volume = vmanCreateVolume(&volumeParams);

	vmanSetUnusedChunkTimeout(config.volume, GetConfigInt("chunk.unused-timeout", 4));
	vmanSetModifiedChunkTimeout(config.volume, GetConfigInt("chunk.modified-timeout", 3));

    config.layers = layers;
    config.layerCount = layerCount;
    config.iterations = GetConfigInt("thread.iterations", 100);
    config.maxSelectionDistance = GetConfigInt("thread.max-selection-distance", 10);
    config.maxSelectionSize = GetConfigInt("thread.max-selection_size", 10);
	config.minWait = GetConfigFloat("thread.min-wait", 0);
	config.maxWait = GetConfigFloat("thread.max-wait", 0);

	const bool statisticsEnabled = GetConfigBool("statistics.enabled", false);
	config.secondsPerStatisticSample = GetConfigFloat("statistics.seconds-per-sample", 0);
	config.statisticsFile = GetConfigString("statistics.file", "");

	tthread::thread* statisticsWriterThread = NULL;
	if(statisticsEnabled)
		statisticsWriterThread = new tthread::thread(StatisticsWriterThread, &config, "StatWriter");

	const int threadCount = GetConfigInt("thread.count", 1);
    for(int i = 0; i < threadCount; ++i)
    {
		char buffer[32];
		sprintf(buffer, "Benchmarker %d", i);
        threads.push_back( new tthread::thread(BenchmarkerThread, &config, buffer) );
    }

	for(int i = 0; i < threads.size(); ++i)
	{
		if(threads[i]->joinable())
			threads[i]->join();
		delete threads[i];
	}

    puts("## Benchmark threads stopped.");

	if(statisticsWriterThread)
	{
		g_StopStatisticsMutex.lock();
		g_StopStatistics = true;
		g_StopStatisticsMutex.unlock();
		g_StopStatisticsCV.notify_all();

		statisticsWriterThread->join();
		delete statisticsWriterThread;
	}

    vmanDeleteVolume(config.volume);
    puts("## Volume deleted.");

    DestroyLayers(layers, layerCount);
    puts("## Success!");

    return 0;
}
