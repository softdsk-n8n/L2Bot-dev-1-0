using Client.Domain.Entities;
using Client.Domain.Service;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Client.Domain.AI.State
{
    public class MoveToSpotState : BaseState
    {
        public MoveToSpotState(AI ai) : base(ai)
        {
        }

        protected override void DoOnEnter(WorldHandler worldHandler, Config config, Hero hero)
        {
            // Do NOT target hero — MoveToSpot just walks to zone center.
            // Previously called RequestAcquireTarget(hero.Id) which made bot target itself.
        }

        protected override void DoExecute(WorldHandler worldHandler, Config config, AsyncPathMoverInterface asyncPathMover, Hero hero)
        {
            if (asyncPathMover.IsLocked)
            {
                return;
            }

            asyncPathMover.MoveAsync(new ValueObjects.Vector3(
                config.Combat.Zone.Center.X,
                config.Combat.Zone.Center.Y,
                hero.Transform.Position.Z
            ), config.Combat.MaxPassableHeight);
        }
    }
}
