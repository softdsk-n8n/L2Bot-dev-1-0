using Client.Domain.Entities;
using Client.Domain.Events;
using Client.Domain.Factories;
using Client.Domain.Helpers;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Client.Domain.Service
{
    public class NpcHandler : EntityHandler<NPC>, EventHandlerInterface<TargetChangedEvent>
    {
        public override void OnCreate(NPC entity)
        {
            entity.Level = npcInfoHelper.GetLevel(entity.NpcId);
            entity.AggroRadius = npcInfoHelper.GetAggroRadius(entity.NpcId);
            // Teon fix: DLL-side isMob/IsMonster() both broken (always return 0/false).
            // Use npcInfo.json AggroRadius to determine hostility instead.
            if (entity.AggroRadius > 0 || npcInfoHelper.IsGuard(entity.NpcId))
            {
                entity.IsHostile = true;
            }
            eventBus.Publish(new CreatureCreatedEvent(entity));
        }

        public override void OnUpdate(NPC entity)
        {
            // Re-apply IsHostile override after DLL data overwrites it via PopulateObject
            if (entity.AggroRadius > 0 || npcInfoHelper.IsGuard(entity.NpcId))
            {
                entity.IsHostile = true;
            }
        }
        public override void OnDelete(NPC entity)
        {
            eventBus.Publish(new CreatureDeletedEvent(entity.Id));
        }

        public void Handle(TargetChangedEvent @event)
        {
            var target = GetEntity(@event.Hero.TargetId);
            if (target == null)
            {
                return;
            }
            @event.Hero.Target = target;
        }

        public NpcHandler(EntityFactoryInterface<NPC> factory, EntityFactoryInterface<Entity> entityFactory, EventBusInterface eventBus, NpcInfoHelperInterface npcInfoHelper) : base(factory, entityFactory)
        {
            this.eventBus = eventBus;
            this.npcInfoHelper = npcInfoHelper;
        }

        private readonly EventBusInterface eventBus;
        private readonly NpcInfoHelperInterface npcInfoHelper;
    }
}
