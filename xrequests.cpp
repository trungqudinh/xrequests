#include <algorithm>
#include <argp.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <stdio.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <curl/curl.h>
#include <thread_pool.hpp>

using namespace std;
typedef std::chrono::high_resolution_clock Clock;

template<typename T, typename Predicate = function<bool(T)>>
class Statistic
{
private:
    T sum_;
    T min_;
    T max_;
    unsigned count_;
    map<string, unsigned> countOn_;
    map<string, Predicate> predicates_;
public:
    Statistic()
    {
        clear();
    }

    map<string, unsigned> getFollowingValue()
    {
        return countOn_;
    }

    void addPredicate(pair<string, Predicate> _predicate)
    {
        addPredicate(_predicate.first, _predicate.second);
    }

    void addPredicate(string _name, Predicate _predicate)
    {
        predicates_[_name] = _predicate;
        countOn_[_name] = 0;
    }

    void addValue(T _value)
    {
        min_ = count_ ? min(min_, _value) : _value;
        max_ = count_ ? max(max_, _value) : _value;
        sum_ += _value;
        count_++;
        for(auto& pred : predicates_)
        {
            if (pred.second(_value))
            {
                countOn_[pred.first]++;
            }
        }
    }

    void clear()
    {
        sum_ = 0;
        min_ = 0;
        max_ = 0;
        count_ = 0;
    }

    T getSum() { return sum_; }
    T getMin() { return min_; }
    T getMax() { return max_; }
    T getMean() { return getSum() / getCount(); }
    unsigned getCount() { return count_; }

};

void printError(string msg)
{
    printf("[ERROR] %s", msg.c_str());
}

typedef struct Arguments
{
    string inputFile;
    string prefix;
    int limit;
    int chunkSize;
    int timeRange;
    int minDistance;
    int timeout;
    void print()
    {
        cout << "inputFile " << inputFile << endl;
    }
} Arguments ;

Arguments defaultArguments = {"", "", 1000, 1000, 1000, 0, 1000};

enum CompressOptions : int
{
    INPUT = 'i',
    LIMIT = 'l',
    PREFIX = 'p',
    CHUNK_SIZE = 0x88,
    TIME_RANGE = 0x89,
    MIN_DISTANCE = 0x90,
    TIME_OUT = 0x91
};

map<CompressOptions, string> ArgumentsDescriptions =
{
    { CompressOptions::TIME_OUT, string("Timeout of a request in millisecond.") + "\nDefault: " + to_string(defaultArguments.timeout) },
    { CompressOptions::LIMIT, string("Number of requests to sent.") + "\nDefault: " + to_string(defaultArguments.limit) },
    { CompressOptions::PREFIX, string("Prefix concatenate to requests.") + " Default: " + defaultArguments.prefix },
    { CompressOptions::CHUNK_SIZE, string("Number of requests per chunk will be sent in TIME_RANGE.") + "\nDefault: \"" + to_string(defaultArguments.chunkSize) + "\""},
    { CompressOptions::TIME_RANGE, string("Range of time in millisecond, that CHUNK_SIZE request will be distributed in.") + "\nDefault: " + to_string(defaultArguments.timeRange) },
    { CompressOptions::MIN_DISTANCE, string("Mininum time between each request in millisecond.")  + "\nDefault: " + to_string(defaultArguments.minDistance) }
};
static struct argp_option options[] =
{
    {"input",  CompressOptions::INPUT, "INPUT_FILE", 0,
        "[Required] Input file contain requests", 0},
    {"limit",  CompressOptions::LIMIT, "LIMIT", 0,
        ArgumentsDescriptions[CompressOptions::LIMIT].c_str(), 0},
    {"prefix",  CompressOptions::PREFIX, "PREFIX", 0,
        ArgumentsDescriptions[CompressOptions::PREFIX].c_str(), 0},
    {"timeout", CompressOptions::TIME_OUT, "TIMEOUT", 0,
        ArgumentsDescriptions[CompressOptions::TIME_OUT].c_str(), 5},
    {"chunk-size", CompressOptions::CHUNK_SIZE, "SIZE", 0,
        ArgumentsDescriptions[CompressOptions::CHUNK_SIZE].c_str(), 5},
    {"time-range", CompressOptions::TIME_RANGE, "RANGE", 0,
        ArgumentsDescriptions[CompressOptions::TIME_RANGE].c_str(), 5},
    {"min-time-distance",  CompressOptions::MIN_DISTANCE, "MIN_DISTANCE", 0,
        ArgumentsDescriptions[CompressOptions::MIN_DISTANCE].c_str(), 5},
    {0, 0, 0, 0, 0, 0}
};

static char args_doc[] = "";

static char doc[] = "Simultaneously send multiple http requests";

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    //  struct arguments *arguments = state->input;

    Arguments *arguments =  static_cast<struct Arguments*>(state->input);
    switch (key)
    {
        case CompressOptions::INPUT:
            arguments->inputFile = arg;
            break;
        case CompressOptions::LIMIT:
            arguments->limit = abs(atoi(arg));
            break;
        case CompressOptions::PREFIX:
            arguments->prefix = arg;
            break;
        case CompressOptions::CHUNK_SIZE:
            arguments->chunkSize = abs(atoi(arg));
            break;
        case CompressOptions::TIME_RANGE:
            arguments->timeRange = abs(atoi(arg));
            break;
        case CompressOptions::MIN_DISTANCE:
            arguments->minDistance = abs(atoi(arg));
            break;
        case CompressOptions::TIME_OUT:
            arguments->timeout = abs(atoi(arg));
            break;
        case ARGP_KEY_END:
            if (arguments->inputFile ==  "")
            {
                printError("--input is required");
                exit(1);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc, 0, 0, 0};

mutex mtx;
Statistic<double> statisticTotal;
Statistic<double> statisticSuccess;

size_t write_data_callback(void *contents, size_t size, size_t nmemb, void* receiver) {
    size_t realsize = size * nmemb;
    std::string& data = *reinterpret_cast<std::string*> (receiver);
    data.append(reinterpret_cast<char*>(contents), realsize);
    return realsize;
}

Arguments get_option(int argc, char** argv)
{
    Arguments arguments = defaultArguments;
    arguments.inputFile = "";
    argp_parse (&argp, argc, argv, 0, 0, &arguments);

    return arguments;
}

double microtime() {
    Clock::time_point t = Clock::now();
    return std::chrono::duration_cast<std::chrono::duration<double>>(t.time_since_epoch()).count();
}

template <typename T>
vector<T> randomSum(T _sum, int _len, T _min = 0, int _factor = 100000 )
{
    if ( _min * _len >= _sum)
    {
        vector<T> res(_len, _sum / _len);
        return res;
    }
    minstd_rand gen(Clock::now().time_since_epoch().count());
    uniform_int_distribution<int> distribution(0, _factor);
    distribution(gen);
    vector<T> res;
    T sum = 0;
    int l = _len;
    while(l--)
    {
        auto value = distribution(gen);
        res.push_back(value);
        sum += value;
    }
    for (auto& i : res)
    {
        i = static_cast<T>((i * 1.0) / sum * (_sum - (_min * _len)) + _min);
    }
    return res;
}

template <typename T>
vector<vector<T>> getChunks(vector<T> _array, int _chunkSize)
{
    vector<vector<T>> res;
    int count = 0;
    for(auto& i : _array)
    {
        if (count++ % _chunkSize == 0)
        {
            res.push_back({});
        }
        res.back().push_back(i);
    }
    return res;
}

string perform_curl(string url, int timeout)
{
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    string data = "";
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_callback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, reinterpret_cast<void*>(&data));

        res = curl_easy_perform(curl);
        if(res != CURLE_OK)
            fprintf(stderr, "error: %s\n",
                    curl_easy_strerror(res));

        curl_easy_cleanup(curl);
    }
    return data;
}

void handleResponse(string response)
{
    cout << response << endl;
}

void handleResponse(string response, double responseTime)
{
    cout << response << endl;
    mtx.lock();
    statisticTotal.addValue(responseTime);
    if (response != "")
        statisticSuccess.addValue(responseTime);
    mtx.unlock();
}

void fetch(string url, int timeout)
{
    auto startTime = microtime();
    string res = "";
    try
    {
        res = perform_curl(url, timeout);
    }
    catch (exception& e)
    {
        cout << "Error: " << e.what() << endl;
    }
    auto endTime = microtime();
    handleResponse(res, endTime - startTime);
}

void fetchAll(vector<string> urls)
{
    ThreadPool pool(50);
    for(auto& url : urls)
    {
        pool.enqueue(fetch, url, 1000);
    }
}

template<typename T>
void printStatistic(Statistic<T> _total, Statistic<T> _success)
{
    auto countOn = _total.getFollowingValue();
    printf("\n======== response times statistic ========\n");
    printf("Total requests: %5d\n", _total.getCount());
    printf("        lowest: %11.5fs\n", _total.getMin());
    printf("       highest: %11.5fs\n", _total.getMax());
    printf("          mean: %11.5fs\n", _total.getMean());
    printf("       success: %5d ~ %6.2f %%\n", _success.getCount(), _success.getCount() * 100.0 / _total.getCount() );
    for(auto& c : countOn)
    {
        auto percent = c.second * 100.0 / _total.getCount();
        printf("%14s: %5d ~ %6.2f %%\n", c.first.c_str(), c.second, percent);
    }

    printf("\nSuccess requests: %5d\n", _success.getCount());
    printf("          lowest: %11.5fs\n", _success.getMin());
    printf("         highest: %11.5fs\n", _success.getMax());
    printf("            mean: %11.5fs\n", _success.getMean());
    countOn = _success.getFollowingValue();
    for(auto& c : countOn)
    {
        auto percent = c.second * 100.0 / _success.getCount();
        printf("%16s: %5d ~ %6.2f %%\n", c.first.c_str(), c.second, percent);
    }
}

int main(int argc, char** argv)
{
    auto arguments = get_option(argc, argv);
    int line = 0;
    string url;
    vector<int> times;
    vector<pair<string, function<bool(double)>>> checker = 
    {
        make_pair("< 1000ms", [](double v) {return v < 1.0;}),
        make_pair(" < 100ms", [](double v) {return v < 0.1;}),
        make_pair("  < 50ms", [](double v) {return v < 0.05;})
    };

    for(auto& c : checker)
    {
        statisticTotal.addPredicate(c);
        statisticSuccess.addPredicate(c);
    }
    ifstream file(arguments.inputFile);

    {
        ThreadPool pool(50);
        bool stop = false;
        while (!stop && line < arguments.limit)
        {
            if (line % arguments.chunkSize == 0)
            {
                times = randomSum<int>(arguments.timeRange, arguments.chunkSize, arguments.minDistance);
            }
            for(auto& t : times)
            {
                if (std::getline(file, url) && line < arguments.limit)
                {
                    if (url != "")
                        pool.enqueue(fetch, url, arguments.timeout);
                    std::this_thread::sleep_for(std::chrono::milliseconds(t));
                    line++;
                }
                else
                {
                    stop = true;
                    break;
                }
            }
        }
    }

    printStatistic(statisticTotal, statisticSuccess);
    file.close();
    return 0;
}