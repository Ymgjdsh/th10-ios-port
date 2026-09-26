#include "../Lifecycle.hpp"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <array>

using touhou::ios::Lifecycle;
int main(){
    // An interruption ending in the background cannot resume the simulation.
    Lifecycle first;
    assert(first.set(Lifecycle::audioInterruption,true));
    assert(!first.set(Lifecycle::inactive,true));
    assert(!first.set(Lifecycle::audioInterruption,false));
    assert(!first.running());
    assert(first.set(Lifecycle::inactive,false));
    assert(first.running());

    // Foreground can arrive before the interruption ends, or be duplicated.
    Lifecycle second;
    assert(second.set(Lifecycle::inactive,true));
    assert(!second.set(Lifecycle::audioInterruption,true));
    assert(!second.set(Lifecycle::inactive,false));
    assert(!second.set(Lifecycle::inactive,false));
    assert(!second.running());
    assert(second.set(Lifecycle::audioInterruption,false));
    assert(second.running());

    // A terminal result remains terminal across either notification sequence.
    assert(second.set(Lifecycle::stopped,true));
    assert(!second.set(Lifecycle::inactive,true));
    assert(!second.set(Lifecycle::audioInterruption,true));
    assert(!second.set(Lifecycle::inactive,false));
    assert(!second.set(Lifecycle::audioInterruption,false));
    assert(!second.running());
    Lifecycle settings;
    assert(settings.set(Lifecycle::settings,true));
    assert(!settings.set(Lifecycle::inactive,true));
    assert(!settings.set(Lifecycle::settings,false));
    assert(!settings.running());
    assert(settings.set(Lifecycle::inactive,false));
    assert(settings.running());
    assert(settings.set(Lifecycle::audioInterruption,true));
    assert(!settings.set(Lifecycle::settings,true));
    assert(!settings.set(Lifecycle::settings,false));
    assert(!settings.running());
    assert(settings.set(Lifecycle::audioInterruption,false));
    // User close, share cancellation and foreground notifications can arrive
    // in any order; duplicate completions may clear only the settings reason.
    std::array<Lifecycle::Reason,3> reasons{Lifecycle::inactive,Lifecycle::audioInterruption,Lifecycle::settings};
    do{
        Lifecycle overlap;
        for(auto reason:reasons)overlap.set(reason,true);
        for(size_t i=0;i<reasons.size();i++){
            const bool last=i==reasons.size()-1;
            assert(overlap.set(reasons[i],false)==last);
            assert(!overlap.set(reasons[i],false));
            assert(overlap.running()==last);
        }
    }while(std::next_permutation(reasons.begin(),reasons.end()));
    Lifecycle terminalSettings;
    terminalSettings.set(Lifecycle::stopped,true);
    terminalSettings.set(Lifecycle::settings,true);
    assert(!terminalSettings.set(Lifecycle::settings,false));
    assert(!terminalSettings.running());
    std::puts("PASS: overlapping lifecycle pauses, notification ordering, terminal state");
}
