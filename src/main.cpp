#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <chrono>

#include <thread>
#include "systeminfo.h"
#include "fileio.h"
#include "image.h"
#include "image_exr.h"
#include "image_jxl.h"
#include "image_mop.h"

#ifdef _DEBUG
const int kRunCount = 1;
#else
const int kRunCount = 3;
#endif

inline std::chrono::high_resolution_clock::time_point time_now()
{
    return std::chrono::high_resolution_clock::now();
}

inline float time_duration_ms(std::chrono::high_resolution_clock::time_point t0)
{
    std::chrono::duration<float, std::milli> dt = std::chrono::high_resolution_clock::now() - t0;
    return dt.count();
}

struct CompressorTypeDesc
{
    const char* name;
    CompressorType cmp;
    const char* color;
    int large;
};

static const CompressorTypeDesc kComprTypes[] =
{
    {"Raw",     CompressorType::Raw,        "a64436", 0}, // 0 - just raw bits read/write
    {"None",    CompressorType::ExrNone,    "a64436", 0}, // 1, red
    {"RLE",     CompressorType::ExrRLE,     "dc74ff", 0}, // 2, purple
    {"PIZ",     CompressorType::ExrPIZ,     "ff9a44", 0}, // 3, orange
    {"Zip",     CompressorType::ExrZIP,     "12b520", 0}, // 4, green
    {"HT256",   CompressorType::ExrHT256,   "0094ef", 0}, // 5, blue
	{"JXL",     CompressorType::Jxl,        "e01010", 0}, // 6, red
    {"Mop",     CompressorType::Mop,        "808080", 0}, // 7, gray
};
constexpr size_t kComprTypeCount = sizeof(kComprTypes) / sizeof(kComprTypes[0]);

struct CompressorDesc
{
    int type;
    int level;
};

static const CompressorDesc kTestCompr[] =
{
    //{ 0, 0 }, // just raw bits read/write
    
    // EXR
#if 1
    //{ 1, 0 }, // None
    { 2, 0 }, // RLE
    { 3, 0 }, // PIZ

    //{ 4, 2 },
    { 4, 4 }, // ZIP default
    //{ 4, 6 },
    //{ 4, 9 },

    { 5, 0 }, // HT256
#endif
    
    // JXL
#if 1
    { 6, 1 },
    { 6, 4 },
    { 6, 7 }, // default level 7
    //{ 6, 9 },
#endif

    // Mop
#if 1
    { 7, 2 }, // default level 2
#endif
};
constexpr size_t kTestComprCount = sizeof(kTestCompr) / sizeof(kTestCompr[0]);

struct ComprResult
{
    size_t rawSize = 0;
    size_t cmpSize = 0;
    double tRead = 0;
    double tWrite = 0;
};
static ComprResult s_ResultRuns[kTestComprCount][kRunCount];
static ComprResult s_Result[kTestComprCount];

static bool TestFile(const char* file_path, int run_index)
{
    const char* fname_part = strrchr(file_path, '/');
    if (fname_part == nullptr)
        fname_part = file_path;
    printf("%s: ", fname_part);
    
    // read the input file
    Image img_in;
    {
        MyIStream mem_in(file_path);
        if (!LoadExrFile(mem_in, img_in))
        {
            printf("ERROR: failed to load EXR file %s\n", file_path);
            return false;
        }
    }
    
    // Note: libjxl currently does not seem to round-trip fp16 subnormals
    // even in full lossless mode, see https://github.com/libjxl/libjxl/issues/3881
    SanitizePixelValues(img_in);

    printf("%ix%i, %i channels, %i bytes/pixel (%.1fMB)\n", int(img_in.width), int(img_in.height), int(img_in.channels.size()), int(img_in.pixels.size()/img_in.width/img_in.height), img_in.pixels.size()/1024.0/1024.0);
    const size_t raw_size = img_in.pixels.size();
    
    // test various compression schemes
    for (size_t cmp_index = 0; cmp_index < kTestComprCount; ++cmp_index)
    {
        const auto& cmp = kTestCompr[cmp_index];
        const CompressorType cmp_type = kComprTypes[cmp.type].cmp;
        double t_write = 0;
        double t_read = 0;

        // save the file with given compressor
        auto t_write_0 = time_now();
        MyOStream mem_out;
        if (cmp_type == CompressorType::Raw)
        {
            mem_out.write(img_in.pixels.data(), (int)img_in.pixels.size());
        }
        else if (cmp_type == CompressorType::Jxl)
        {
            if (!SaveJxlFile(mem_out, img_in, cmp.level))
            {
                printf("ERROR: file could not be saved to JXL %s\n", fname_part);
                return false;
            }
        }
        else if (cmp_type == CompressorType::Mop)
        {
            if (!SaveMopFile(mem_out, img_in, cmp.level))
            {
                printf("ERROR: file could not be saved to MOP %s\n", fname_part);
                return false;
            }
        }
        else
        {
            if (!SaveExrFile(mem_out, img_in, cmp_type, cmp.level))
            {
                printf("ERROR: file could not be saved to EXR %s\n", fname_part);
                return false;
            }
        }
        t_write = time_duration_ms(t_write_0) / 1000.0f;
        size_t out_size = mem_out.size();
        
        // read the file back
        Image img_got;
        auto t_read_0 = time_now();
        MyIStream mem_got_in(mem_out.data(), mem_out.size());
        if (cmp_type == CompressorType::Raw)
        {
            img_got.width = img_in.width;
            img_got.height = img_in.height;
            img_got.pixels.resize(img_in.pixels.size());
            memcpy(img_got.pixels.data(), mem_got_in.data(), img_got.pixels.size());
        }
        else if (cmp_type == CompressorType::Jxl)
        {
            if (!LoadJxlFile(mem_got_in, img_got))
            {
                printf("ERROR: file could not be loaded from JXL %s\n", fname_part);
                return false;
            }
        }
        else if (cmp_type == CompressorType::Mop)
        {
            if (!LoadMopFile(mem_got_in, img_got))
            {
                printf("ERROR: file could not be loaded from MOP %s\n", fname_part);
                return false;
            }
        }
        else
        {
            if (!LoadExrFile(mem_got_in, img_got))
            {
                printf("ERROR: file could not be loaded from EXR %s\n", fname_part);
                return false;
            }
        }
        t_read = time_duration_ms(t_read_0) / 1000.0f;
        if (!CompareImages(img_in, img_got))
        {
            printf("ERROR: file did not roundtrip exactly with compression %s\n", kComprTypes[cmp.type].name);
            //SaveExrFile("_outfile.got.exr", img_got, CompressorType::ExrZIP, 0);
            return false;
        }

        auto& res = s_ResultRuns[cmp_index][run_index];
        res.rawSize += raw_size;
        res.cmpSize += out_size;
        res.tRead += t_read;
        res.tWrite += t_write;
    }
    
    return true;
}

static void WriteReportRow(FILE* fout, uint64_t gotTypeMask, size_t cmpIndex, double xval, double yval)
{
    const int cmpLevel = kTestCompr[cmpIndex].level;
    const size_t typeIndex = kTestCompr[cmpIndex].type;
    const char* cmpName = kComprTypes[typeIndex].name;

    for (size_t ii = 0; ii < typeIndex; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
    fprintf(fout, ",%.2f,'", yval);
    if (cmpLevel != 0)
        fprintf(fout, "%s%i", cmpName, cmpLevel);
    else
        fprintf(fout, "%s", cmpName);
    fprintf(fout, ": %.3f ratio, %.1f MB/s'", xval, yval);
    for (size_t ii = typeIndex+1; ii < kComprTypeCount; ++ii)
    {
        if ((gotTypeMask & (1ull<<ii)) == 0)
            continue;
        fprintf(fout, ",null,null");
    }
}


static void WriteReportFile(int threadCount, int fileCount, size_t fullSize)
{
    std::string curTime = sysinfo_getcurtime();
    std::string outName = curTime + ".html";
    FILE* fout = fopen(outName.c_str(), "wb");
    fprintf(fout,
R"(<script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script>
<center style='font-family: Arial;'>
<p><b>EXR/JXL compression ratio vs throughput</b>, %i files (%.1fMB) <span style='color: #ccc'>%s, %i runs</span</p>
<div style='border: 1px solid #ccc;'>
<div id='chart_w' style='width: 640px; height: 640px; display:inline-block;'></div>
<div id='chart_r' style='width: 640px; height: 640px; display:inline-block;'></div>
</div>
<p>%s, %s, %i threads</p>
<script type='text/javascript'>
google.charts.load('current', {'packages':['corechart']});
google.charts.setOnLoadCallback(drawChart);
function drawChart() {
var dw = new google.visualization.DataTable();
var dr = new google.visualization.DataTable();
dw.addColumn('number', 'Ratio');
dr.addColumn('number', 'Ratio');
)",
            fileCount, fullSize/1024.0/1024.0, curTime.c_str(), kRunCount,
            sysinfo_getplatform().c_str(), sysinfo_getcpumodel().c_str(), threadCount);

    uint64_t gotCmpTypeMask = 0;
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        gotCmpTypeMask |= 1ull << kTestCompr[cmpIndex].type;
    }

    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout,
R"(dw.addColumn('number', '%s'); dw.addColumn({type:'string', role:'tooltip'});
dr.addColumn('number', '%s'); dr.addColumn({type:'string', role:'tooltip'});
)", cmp.name, cmp.name);
    }
    fprintf(fout, "dw.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tWrite;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");
    fprintf(fout, "dr.addRows([\n");
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& res = s_Result[cmpIndex];
        double ratio = (double)res.rawSize/(double)res.cmpSize;
        fprintf(fout, "[%.3f", ratio);
        double perf = res.rawSize / (1024.0*1024.0) / res.tRead;
        WriteReportRow(fout, gotCmpTypeMask, cmpIndex, ratio, perf);
        fprintf(fout, "]%s\n", cmpIndex == kTestComprCount-1 ? "" : ",");
    }
    fprintf(fout, "]);\n");

    fprintf(fout,
R"(var options = {
    title: 'Writing',
    pointSize: 18,
    series: {
)");
    int seriesIdx = 0;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull<<cmpType)) == 0)
            continue;
        fprintf(fout, "        %i: {pointSize: %i},\n", seriesIdx, kComprTypes[cmpType].large ? 18 : 8);
        ++seriesIdx;
    }
    fprintf(fout,
R"(        100:{}},
    hAxis: {title: 'Compression ratio', viewWindow: {min:1.0,max:3.0}},
    vAxis: {title: 'Writing, MB/s', viewWindow: {min:0, max:6250}},
    chartArea: {left:60, right:10, top:50, bottom:50},
    legend: {position: 'top'},
    colors: [
)");
    bool firstCol = true;
    for (size_t cmpType = 0; cmpType < kComprTypeCount; ++cmpType)
    {
        if ((gotCmpTypeMask & (1ull << cmpType)) == 0)
            continue;
        if (!firstCol)
            fprintf(fout, ",");
        firstCol = false;
        const auto& cmp = kComprTypes[cmpType];
        fprintf(fout, "'#%s'", cmp.color);
    }

    fprintf(fout,
R"(]
};
var chw = new google.visualization.ScatterChart(document.getElementById('chart_w'));
chw.draw(dw, options);

options.title = 'Reading';
options.vAxis.title = 'Reading, MB/s';
options.vAxis.viewWindow.max = 6250;
var chr = new google.visualization.ScatterChart(document.getElementById('chart_r'));
chr.draw(dr, options);
}
</script>
)");
    
    fclose(fout);
}

int main(int argc, const char** argv)
{
    if (argc < 2) {
        printf("USAGE: test_exr_htj2k_jxl <input exr files>\n");
        return 1;
    }
    unsigned nThreads = std::thread::hardware_concurrency();
//#ifdef _DEBUG
//    nThreads = 0;
//#endif
    printf("Setting EXR/JXL to %i threads\n", nThreads);
    InitExr(nThreads);
    InitJxl(nThreads);
    InitMop(nThreads);

    for (int ri = 0; ri < kRunCount; ++ri)
    {
        printf("Run %i/%i...\n", ri+1, kRunCount);
        for (int fi = 1; fi < argc; ++fi)
        {
            bool ok = TestFile(argv[fi], ri);
            if (!ok)
                return 1;
        }
        
        for (int ci = 0; ci < kTestComprCount; ++ci)
        {
            const ComprResult& res = s_ResultRuns[ci][ri];
            ComprResult& dst = s_Result[ci];
            if (ri == 0)
            {
                dst = res;
            }
            else
            {
                if (res.cmpSize != dst.cmpSize)
                {
                    printf("ERROR: compressor case %i non deterministic compressed size (%zi vs %zi)\n", ci, res.cmpSize, dst.cmpSize);
                    return 1;
                }
                if (res.rawSize != dst.rawSize)
                {
                    printf("ERROR: compressor case %i non deterministic raw size (%zi vs %zi)\n", ci, res.rawSize, dst.rawSize);
                    return 1;
                }
                if (res.tRead < dst.tRead) dst.tRead = res.tRead;
                if (res.tWrite < dst.tWrite) dst.tWrite = res.tWrite;
            }
        }
    }

    WriteReportFile(nThreads, argc-1, s_Result[0].rawSize);
    printf("==== Summary (%i files, %i runs):\n", argc-1, kRunCount);
    for (size_t cmpIndex = 0; cmpIndex < kTestComprCount; ++cmpIndex)
    {
        const auto& cmp = kTestCompr[cmpIndex];
        const auto& res = s_Result[cmpIndex];

        double perfWrite = res.rawSize / (1024.0*1024.0) / res.tWrite;
        double perfRead = res.rawSize / (1024.0*1024.0) / res.tRead;
        printf("  %6s: %7.1f MB (%5.3fx) W: %6.3f s (%5.0f MB/s) R: %6.3f s (%5.0f MB/s)\n",
               kComprTypes[cmp.type].name,
               res.cmpSize/1024.0/1024.0,
               (double)res.rawSize/(double)res.cmpSize,
               res.tWrite,
               perfWrite,
               res.tRead,
               perfRead);
    }
    
    return 0;
}
