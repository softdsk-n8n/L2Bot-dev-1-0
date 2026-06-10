using Client.Domain.Entities;
using Client.Domain.Service;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Client.Domain.AI.State
{
    public class IdleState : BaseState
    {
        public IdleState(AI ai) : base(ai)
        {
        }

        protected override void DoExecute(WorldHandler worldHandler, Config config, AsyncPathMoverInterface asyncPathMover, Hero hero)
        {
            // Auto-update combat zone center while Idle so the bot follows the player.
            // If zone is still at the default (0,0,0) or the player has wandered
            // significantly away from it, recenter the zone on the hero.
            if (hero.Transform != null && !hero.Transform.Position.Equals(Vector3.Zero))
            {
                var center = config.Combat.Zone.Center;
                var pos = hero.Transform.Position;
                var dist = (float)Math.Sqrt(
                    (center.X - pos.X) * (center.X - pos.X) +
                    (center.Y - pos.Y) * (center.Y - pos.Y) +
                    (center.Z - pos.Z) * (center.Z - pos.Z)
                );

                if (center.Equals(Vector3.Zero) || dist > config.Combat.Zone.Radius * 0.8f)
                {
                    config.Combat.Zone.Center = pos;
                }
            }
        }
    }
}
