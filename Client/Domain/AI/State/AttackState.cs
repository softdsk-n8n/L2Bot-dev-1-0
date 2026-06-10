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
            // Immediate soulshot reaction: toggle on/off every tick according to AutoUseShots checkbox.
            // This is needed because DoOnLeave only fires when leaving Attack state —
            // if the user unchecks AutoUseShots while the bot is still attacking,
            // soulshots would stay on until the mob dies or state changes.
            if (config.Combat.AutoUseShots)
            {
                var weapon = worldHandler.GetEquippedWeapon();
                var weaponGrade = weapon != null && weapon.IsEquipped ? weapon.CrystalType : Enums.CrystalTypeEnum.None;
                var shots = worldHandler.GetShotItems(weaponGrade);
                foreach (var item in shots)
                {
                    if (!item.IsAutoused)
                    {
                        worldHandler.RequestToggleAutouseSoulshot(item.Id);
                    }
                }
            }
            else
            {
                // When turning off shots, disable every autoused shot regardless of grade.
                // The user may have shots of a different grade than their current weapon
                // (e.g. S-grade shots with an A-grade weapon). We must still turn them off.
                foreach (var item in worldHandler.GetShotItems())
                {
                    if (item.IsAutoused)
                    {
                        worldHandler.RequestToggleAutouseSoulshot(item.Id);
                    }
                }
            }

            if (hero.Target == null)
            {
                return;
            }

            // Dynamic soulshot management: react to AutoUseShots checkbox changes in real-time,
            // not just on state transitions. Fixes issue where disabling shots mid-combat had no effect.
            if (hero.Target != null)
            {
                var weapon = worldHandler.GetEquippedWeapon();
                var weaponGrade = weapon != null && weapon.IsEquipped ? weapon.CrystalType : Enums.CrystalTypeEnum.None;
                var shots = worldHandler.GetShotItems(weaponGrade);
                foreach (var item in shots)
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
                var weapon = worldHandler.GetEquippedWeapon();
                var weaponGrade = weapon != null && weapon.IsEquipped ? weapon.CrystalType : Enums.CrystalTypeEnum.None;
                foreach (var item in worldHandler.GetShotItems(weaponGrade))
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
                var weapon = worldHandler.GetEquippedWeapon();
                var weaponGrade = weapon != null && weapon.IsEquipped ? weapon.CrystalType : Enums.CrystalTypeEnum.None;
                foreach (var item in worldHandler.GetShotItems(weaponGrade))
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
