using Client.Domain.DTO;
using Client.Domain.Service;
using Client.Domain.ValueObjects;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Client.Infrastructure.Service
{
    /// <summary>
    /// Simple pathfinder that returns a direct path (single segment).
    /// No geodata required — just goes in a straight line.
    /// </summary>
    public class SimplePathfinder : PathfinderInterface
    {
        public List<PathSegment> FindPath(Vector3 start, Vector3 end, ushort maxPassableHeight)
        {
            return new List<PathSegment>
            {
                new PathSegment { From = start, To = end }
            };
        }

        public bool HasLineOfSight(Vector3 start, Vector3 end)
        {
            // Without geodata, assume line of sight always exists
            return true;
        }
    }
}
