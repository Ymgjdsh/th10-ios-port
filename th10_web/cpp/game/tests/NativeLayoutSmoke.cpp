#include "../AnmVm.hpp"
#include "../EclContext.hpp"
#include "../GameProgression.hpp"
#include "../Projectile.hpp"
#include "../Replay.hpp"
#include "../ScoreData.hpp"
#include "../ScorePopups.hpp"

// Exercise runtime objects whose layouts grow with native pointers, while
// keeping the original score/replay/resource records fixed on disk.
extern "C" int native_layout_smoke() {
    using namespace th10;
    static_assert(sizeof(CharacterRecord)==0x437c);
    static_assert(sizeof(ScoreSettings)==0x448);
    static_assert(sizeof(ReplayInfo)==0x64 && sizeof(ReplayStage)==0x1c4);
    static_assert(sizeof(ProjectileCommand)==0x18 && sizeof(EclInstruction)==16);

    AnmVm animation{};
    animation.position={12,34,56};animation.owner_tag=42;
    animation.initialize();
    if(animation.flags!=7 || animation.color!=0xffffffff) return 1;
    if(animation.position.x!=12 || animation.position.y!=34 || animation.position.z!=56 || animation.owner_tag!=42) return 2;
    if(animation.registry_node.value!=&animation || animation.child_node.value!=&animation) return 3;
    if(animation.extra_integer_variables[0] || animation.extra_integer_variables[1]) return 4;

    EclContext context{};
    const auto first_bits=static_cast<std::uintptr_t>(UINT64_C(0x1234567887654320));
    const auto second_bits=static_cast<std::uintptr_t>(UINT64_C(0x7654321012345670));
    context.push_return_instruction(reinterpret_cast<EclInstruction*>(first_bits));
    context.push_return_instruction(reinterpret_cast<EclInstruction*>(second_bits));
    if(context.stack.top!=8) return 5;
    context.pop_return_instruction();
    if(reinterpret_cast<std::uintptr_t>(context.instruction)!=second_bits || context.stack.top!=4) return 6;
    context.pop_return_instruction();
    if(reinterpret_cast<std::uintptr_t>(context.instruction)!=first_bits || context.stack.top) return 7;
    context.push_return_instruction(nullptr);context.pop_return_instruction();
    if(context.instruction || context.stack.top) return 8;

    static ScoreData scores{};
    GameEconomy game{};game.character=1;game.shot_type=2;game.difficulty=3;game.stage=6;
    ScoreStatistics statistics{&scores};
    statistics.unlock_stage(game);statistics.count_clear(game);
    const auto* bytes=reinterpret_cast<const u8*>(scores.characters);
    const auto stage_offset=5*sizeof(CharacterRecord)+0x4d8+(6+3*6)*8;
    const auto clear_offset=5*sizeof(CharacterRecord)+0x4c8+3*4;
    for(std::size_t i=0;i<sizeof(scores.characters);++i) {
        const u8 expected=(i==stage_offset || i==stage_offset+1 || i==clear_offset)?1:0;
        if(bytes[i]!=expected) return 9;
    }
    for(auto byte:scores.settings.statistics) if(byte) return 10;
    return 0;
}

#ifndef TH10_SMOKE_NO_MAIN
int main(){return native_layout_smoke();}
#endif
