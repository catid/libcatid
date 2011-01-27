/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
    Unit test for text compression
*/

#include <cat/AllCodec.hpp>
#include <iostream>
#include <conio.h> // getch()
using namespace std;
using namespace cat;

#include <cat/codec/ChatText.stats>

//#define GENERATING_TABLE

int main(int argc, const char **argv)
{
    if (!InitializeFramework("TextCompress.txt"))
	{
		FatalStop("Unable to initialize framework!");
	}

#ifndef GENERATING_TABLE
    if (!TextStatsCollector::VerifyTableIntegrity(ChatText))
    {
        WARN("Text Compression Test") << "Table integrity check failed";
    }
/*    else if (argc <= 1)
    {
        WARN("blah") << "Specify a file to read";
    }*/
    else
#endif
    {
#ifdef GENERATING_TABLE
        TextStatsCollector *collector = new TextStatsCollector();
#endif

        u32 compressed = 0;
        u32 uncompressed = 0;

        int dmax = 32768;
        int cmax = dmax*16;
        char *line = new char[dmax];
        char *comp = new char[cmax];
        char *decomp = new char[cmax];

        float worst = 0;

        const char *Files[] = {
            "bib.txt",
            "book1.txt",
            "book2.txt",
            "news.txt",
            0
        };

        double bratios[1000] = {0};
        double aratios[1000] = {0};
        int total[1000] = {0};
        double wratios[1000] = {0};

        for (int ii = 0; ii < 1000; ++ii)
        {
            bratios[ii] = 1;
        }

        double dtime = 0, ctime = 0;
        u32 linect = 0;
        int findex = 0;
        int longest = 0;
        for (;;)
        {
            const char *fname = Files[findex++];
            if (!fname) break;

            //std::ifstream file(argv[ii]);
            std::ifstream file(fname);
            if (!file)
            {
                WARN("Text Compression Test") << "File error";
            }
            else
            {
                for (;;)
                {
                    file.getline(line, dmax, '\n');
                    if (file.eof()) break;
                    ++linect;

                    //WARN("Text Compression Test") << "line: " << line;

                    int chars = 0;
                    char *x = line;
                    do
                    {
                        //WARN("Text Compression Test") << "char: " << (int)*x;
#ifdef GENERATING_TABLE
                        collector->Tally(*x);
#endif
                        ++chars;
                    } while (*x++);

#ifndef GENERATING_TABLE
                    uncompressed += chars;

                    double start = Clock::usec();
                    RangeEncoder re(comp, cmax);
                    re.Text(line, ChatText);
                    re.Finish();
                    ctime += Clock::usec() - start;
                    if (re.Fail())
                    {
                        WARN("Text Compression Test") << "Compression failure!";
                        WARN("Text Compression Test") << "txt: " << chars;
                    }
                    else
                    {
                        int used = re.Used();
                        compressed += used;

                        start = Clock::usec();
                        RangeDecoder rd(comp, used);
                        int count = rd.Text(decomp, dmax, ChatText) + 1;
                        dtime += Clock::usec() - start;

                        if (rd.Remaining() > 0)
                        {
                            WARN("Text Compression Test") << "ERROR: Unread bytes remaining";
                        }

                        float ratio = used / (float)count;
                        if (worst < ratio)
                        {
                            worst = ratio;
                            WARN("worst") << "origin   : " << line;
                        }

                        if (chars > longest)
                            longest = chars;

                        aratios[chars] += ratio;
                        total[chars]++;

                        if (wratios[chars] < ratio)
                        {
                            wratios[chars] = ratio;
                        }

                        if (bratios[chars] > ratio)
                        {
                            bratios[chars] = ratio;
                        }

                        if (used > count + 1)
                        {
                            WARN("Text Compression Test") << "ERROR: More than one extra byte emitted";
                        }

                        if (count != chars || memcmp(decomp, line, chars))
                        {
                            WARN("Text Compression Test") << "Decompression failure!";
                            WARN("Text Compression Test") << "txt.size : " << chars;
                            WARN("Text Compression Test") << "comp.size: " << used;
                            WARN("Text Compression Test") << "origin   : " << line;
                            WARN("Text Compression Test") << "decomp   : " << decomp;
                            WARN("Text Compression Test") << "out.size : " << count;
                        }
                    }
#endif
                }

                file.close();
            }
        }

#ifndef GENERATING_TABLE
        cout << "-----------------Worst ratios:" << endl;
        for (int ii = 0; ii <= longest; ++ii)
        {
            cout << ii << " letters -> " << wratios[ii] << endl;
        }

        cout << endl << "-----------------Best ratios:" << endl;
        for (int ii = 0; ii <= longest; ++ii)
        {
            cout << ii << " letters -> " << bratios[ii] << endl;
        }

        double ratio_grouped[1000] = {0};
        int total_grouped[1000] = {0};
        int highest = 0;

        cout << endl << "-----------------Average ratios:" << endl;
        for (int ii = 2; ii <= longest; ++ii)
        {
            if (total[ii])
            {
                ratio_grouped[ii / 10] += aratios[ii];
                total_grouped[ii / 10] += total[ii];
                cout << ii << " letters -> " << aratios[ii]/(double)total[ii] << endl;
                highest = ii / 10;
            }
        }

        cout << endl << "-----------------Summary:" << endl;
        for (int ii = 0; ii <= highest; ++ii)
        {
            cout << "For messages from " << ii * 10 << " to " << ((ii+1) * 10 - 1) << " characters, average ratio = " << ratio_grouped[ii]/(double)total_grouped[ii] << endl;
        }

        delete []line;
        delete []comp;
        delete []decomp;

        WARN("Text Compression Test") << "Worst message compression ratio: " << worst;
        WARN("Text Compression Test") << "uncompressed = " << uncompressed;
        WARN("Text Compression Test") << "compressed   = " << compressed;
        WARN("Text Compression Test") << "Compression rate = " << uncompressed / ctime << " MB/s";
        WARN("Text Compression Test") << "Decompression rate = " << uncompressed / dtime << " MB/s";
        WARN("Text Compression Test") << "Average input length = " << uncompressed / linect;
        WARN("Text Compression Test") << "Compression ratio = " << compressed * 100.0f / uncompressed;
        WARN("Text Compression Test") << "Table bytes = " << sizeof(_ChatText);
#else
        ofstream ofile("ChatText.stats");
        if (!ofile)
        {
            WARN("Text Compression Test") << "Unable to open file";
        }
        else
        {
            WARN("Text Compression Test") << collector->GenerateMinimalStaticTable("ChatText", ofile);
        }
        delete collector;
#endif
    }

    //Random::ref()->init(0);

    INFO("Launcher") << "** Press any key to close.";

    while (!getch())
        Sleep(100);

	ShutdownFramework(true);

    return 0;
}
