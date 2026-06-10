using Client.Domain.AI.Combat;
using Client.Domain.Entities;
using Client.Domain.Service;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Client.Domain.AI.State
{
    public class AttackState : BaseState
    {
        public AttackState(AI ai) : base(ai)
        {
        }

        protected override void DoExecute(WorldHandler worldHandler, Config config, AsyncPathMoverInterface asyncPathMover, Hero hero)
        {
            // Reactive soulshot toggle: works every tick in Attack state.
            // We intentionally do NOT filter by weapon grade. The user may only
            // have shots of a different grade (e.g. S-grade shots with A-grade
            // weapon). The game prevents incompatible consumption on its own;
            // our job is to toggle whatever soulshots the user actually has.
            foreach (var item in worldHandler.GetShotItems())
            {
                if (config.Combat.AutoUseShots && !item.IsAutoused)
                {
                    worldHandler.RequestToggleAutouseSoulshot(item.Id);
                }
                else if (!config.Combat.AutoUseShots && item.IsAutoused)
                {
                    worldHandler.RequestToggleAutouseSoulshot(item.Id);
                }
            }

            if (hero.Target == null)
            {
                return;
            }

            if (!config.Combat.UseOnlySkills)
            {
                worldHandler.RequestAttackOrFollow(hero.Target.Id);
            }

            if (config.Combat.SpoilIfPossible)
            {
                NPC? npc = hero.Target as NPC;
                var spoil = worldHandler.GetSkillById(config.Combat.SpoilSkillId);
                if (spoil != null && npc != null && npc.SpoilState == Enums.SpoilStateEnum.None)
                {
                    var excluded = config.Combat.ExcludedSpoilMobIds;
                    var included = config.Combat.IncludedSpoilMobIds;
                    if (!excluded.ContainsKey(npc.NpcId) && (included.Count == 0 || included.ContainsKey(npc.NpcId)))
                    {
                        if (spoil.IsReadyToUse && hero.VitalStats.Mp >= spoil.Cost)
                        {
                            worldHandler.RequestUseSkill(spoil.Id, false, false);
                        }
                    }
                }
            }

            var skill = Helper.GetSkillByConfig(worldHandler, config, hero, hero.Target);
            if (skill != null && skill.IsReadyToUse && hero.VitalStats.Mp >= skill.Cost)
            {
                worldHandler.RequestUseSkill(skill.Id, false, false);
            }
        }

        protected override void DoOnEnter(WorldHandler worldHandler, Config config, Hero hero)
        {
            if (config.Combat.AutoUseShots)
            {
                foreach (var item in worldHandler.GetShotItems())
                {
                    if (!item.IsAutoused)
                    {
                        worldHandler.RequestToggleAutouseSoulshot(item.Id);
                    }
                }
            }
        }

        protected override void DoOnLeave(WorldHandler worldHandler, Config config, Hero hero)
        {
            if (!config.Combat.AutoUseShots)
            {
                foreach (var item in worldHandler.GetShotItems())
                {
                    if (item.IsAutoused)
                    {
                        worldHandler.RequestToggleAutouseSoulshot(item.Id);
                    }
                }
            }
        }
    }
}
