// ovctl — drive a running preview's parameters through MIT shared memory.
//
//   ovctl --list                       what is currently being driven
//   ovctl set pq1Gain 18              approach 18 dB with the default 0.35 s
//   ovctl set pq1Gain 18 --tau 1.2    slower, like a careful drag
//   ovctl set pq1Gain 18 --tau 0      jump (what a preset load does)
//   ovctl sweep pq1Gain -12 18 --time 3 --step 0.05
//                                     a continuous drag, one small step every
//                                     50 ms, reaching 18 in 3 s
//   ovctl release                      forget every slot
//
// The preview must have been started with --ctl <name>; both sides open the same
// POSIX shared-memory block.  Nothing here talks to the process directly, so it
// works whether the UI is responding or not — which is the point when the thing
// being measured is whether the UI responds.
#include "OvCtl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

static void usage()
{
    std::fprintf (stderr,
        "usage: ovctl [--name N] <command>\n"
        "  command:\n"
        "    set <param> <value> [--tau S]\n"
        "    sweep <param> <from> <to> [--time S] [--step S]\n"
        "    list\n"
        "    release\n");
}

int main (int argc, char** argv)
{
    std::string name = "ovctl";
    std::string cmd;
    std::string param;
    double a = 0, b = 0, tau = ovctl::kDefaultTau, timeS = 2.0, stepS = 0.05;

    for (int i = 1; i < argc; ++i)
    {
        const std::string s (argv[i]);
        auto next = [&] () -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };

        if (s == "--name")        name = next();
        else if (s == "--tau")    tau = std::atof (next());
        else if (s == "--time")   timeS = std::atof (next());
        else if (s == "--step")   stepS = std::atof (next());
        else if (s == "-h" || s == "--help") { usage(); return 0; }
        else if (cmd.empty())     cmd = s;
        else if (param.empty())   param = s;
        else if (a == 0.0 && b == 0.0) a = std::atof (s.c_str());
        else                      b = std::atof (s.c_str());
    }

    if (cmd.empty()) { usage(); return 2; }

    // ATTACH, do not create: creating truncates the block and would wipe
    // whatever the previous command set.
    ovctl::Block* blk = ovctl::attach (name);
    if (blk == nullptr)
    {
        std::fprintf (stderr, "ovctl: cannot map /%s (run the preview with --ctl %s)\n",
                      name.c_str(), name.c_str());
        return 1;
    }

    int rc = 0;
    if (cmd == "set")
    {
        if (param.empty()) { usage(); rc = 2; }
        else if (! ovctl::setParam (blk, param, a, tau))
            std::fprintf (stderr, "ovctl: no free slot for %s\n", param.c_str()), rc = 1;
        else
            std::printf ("set %s = %g (tau %g)\n", param.c_str(), a, tau);
    }
    else if (cmd == "sweep")
    {
        if (param.empty()) { usage(); rc = 2; }
        else
        {
            // A drag is a STREAM of small changes, not one target.  Writing one
            // value per step and letting each settle for one step interval is
            // what a pointer moving at a steady speed produces.
            const int steps = int (std::max (1.0, timeS / std::max (0.005, stepS)));
            for (int i = 1; i <= steps; ++i)
            {
                const double t = double (i) / steps;
                const double v = a + (b - a) * t;
                ovctl::setParam (blk, param, v, 0.0);       // instant per step
                std::this_thread::sleep_for (
                    std::chrono::duration<double> (std::max (0.005, stepS)));
            }
            std::printf ("swept %s %g -> %g over %g s in %d steps\n",
                         param.c_str(), a, b, timeS, steps);
        }
    }
    else if (cmd == "list")
    {
        int n = 0;
        for (int i = 0; i < ovctl::kSlots; ++i)
        {
            const auto& s = blk->entries[i];
            if (s.used.load() == 0) continue;
            std::printf ("  %-24s target %-12g tau %.3f  seq %u\n",
                         s.id, double (s.target), double (s.tau), s.seq.load());
            ++n;
        }
        std::printf ("%d slot(s)\n", n);
    }
    else if (cmd == "release")
    {
        for (int i = 0; i < ovctl::kSlots; ++i)
            blk->entries[i].used.store (0);
        blk->seq.fetch_add (1);
        std::printf ("released\n");
    }
    else
    {
        usage();
        rc = 2;
    }

    ovctl::unmap (blk);
    return rc;
}
