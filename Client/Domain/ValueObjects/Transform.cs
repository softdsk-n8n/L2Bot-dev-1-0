using Client.Domain.Common;
using System;

namespace Client.Domain.ValueObjects
{
    public class Transform : ObservableObject
    {
        private Vector3 position;
        private Vector3 rotation;
        private Vector3 velocity;
        private Vector3 acceleration;

        public Vector3 Position
        {
            get => position;
            set
            {
                // Always propagate — PopulateObject creates new Vector3 instances,
                // so reference comparison is unreliable. Update X/Y/Z on existing
                // Vector3 to preserve PropertyChanged subscribers.
                position.X = value.X;
                position.Y = value.Y;
                position.Z = value.Z;
                OnPropertyChanged();
            }
        }
        public Vector3 Rotation
        {
            get => rotation;
            set
            {
                if (value != rotation)
                {
                    rotation.X = value.X;
                    rotation.Y = value.Y;
                    rotation.Z = value.Z;
                    OnPropertyChanged();
                    OnPropertyChanged("Direction");
                }
            }
        }

        private void Rotation_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == "Y")
            {
                OnPropertyChanged("Direction");
            }
        }

        public Vector3 Velocity
        {
            get => velocity;
            set
            {
                if (value != velocity)
                {
                    velocity.X = value.X;
                    velocity.Y = value.Y;
                    velocity.Z = value.Z;
                    OnPropertyChanged();
                }
            }
        }
        public Vector3 Acceleration
        {
            get => acceleration;
            set
            {
                if (value != acceleration)
                {
                    acceleration.X = value.X;
                    acceleration.Y = value.Y;
                    acceleration.Z = value.Z;
                    OnPropertyChanged();
                }
            }
        }

        public Vector3 Direction
        {
            get
            {
                float deg = Rotation.Y / 65535 * 2 * MathF.PI;

                return new Vector3(MathF.Cos(deg), MathF.Sin(deg), 0);
            }
        }

        public bool IsMoving
        {
            get
            {
                return !velocity.ApproximatelyEquals(Vector3.Zero, 0.0001f);
            }
        }

        public Transform(Vector3 position, Vector3 rotation, Vector3 velocity, Vector3 acceleration)
        {
            this.position = position;
            this.rotation = rotation;
            this.velocity = velocity;
            this.acceleration = acceleration;
            rotation.PropertyChanged += Rotation_PropertyChanged;
        }
    }
}
