using Client.Domain.Entities;
using Client.Domain.Service;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Client.Domain.AI.State
{
    public class RestState : BaseState
    {
        public RestState(AI ai) : base(ai)
        {
        }

        protected override void DoOnEnter(WorldHandler worldHandler, Config config, Hero hero)
        {
            // Do NOT call RequestAcquireTarget(hero.Id) — targeting yourself while sitting
            // is unnecessary and causes the bot to appear to "target itself" in the UI.
            // The game does not require an explicit target to sit and rest.
        }

        protected override void DoExecute(WorldHandler worldHandler, Config config, AsyncPathMoverInterface asyncPathMover, Hero hero)
        {
            if (!hero.IsStanding)
            {
                return;
            }

            worldHandler.RequestSit();
        }

        protected override void DoOnLeave(WorldHandler worldHandler, Config config, Hero hero)
        {
            if (hero.IsStanding)
            {
                return;
            }

            worldHandler.RequestStand();
        }
    }
}
