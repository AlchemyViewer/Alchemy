
#define IK_OUT_OF_REACH -1
#define IK_TOO_CLOSE -2
#define IK_SUCCESS 1

// Simple inverse kinematics via trigonemetry
TrigIK(
    vector position, // relative position, only uses x (forward/back) and z (up/down)
    float joint0, // First joint length
    float joint1, // Second joint length
    vector axis, // Axis that the joint rotates on
    integer complementary // Whether to use complementary angle on result
) {
    position.y = 0;
    float distance = llVecMag(position);
    
    // If we can't reach it, just extend out
    if(distance > joint0 + joint1)
    {
        IKr = IK_OUT_OF_REACH;
        IK1 = llEuler2Rot(-axis * llAtan2(position.z, position.x));
        IK2 = ZERO_ROTATION;
        return;
    }
    
    float distanceSq = distance * distance;
    float joint0Sq = joint0 * joint0;
    float joint1Sq = joint1 * joint1;
    float x = (joint0Sq - joint1Sq + distanceSq) / (2 * joint0 * distance);
    float y = (joint0Sq + joint1Sq - distanceSq) / (2 * joint0 * joint1);
    float q0 = llAtan2(position.z, position.x);
    
    // Too close
    if(x > 1 || x < -1 || y > 1 || y < -1)
    {
        IKr = IK_TOO_CLOSE;
        IK1 = llEuler2Rot(-axis *( q0));
        IK2 = ZERO_ROTATION;
        return;
    }
    
    // There's two different ways to reach the same point
    float q1; float q2;
    if(complementary)
    {
        q1 = q0 - llAcos(x);
        q2 = PI - llAcos(y);
    }
    else
    {
        q1 = q0 + llAcos(x);
        q2 = PI + llAcos(y);
    }
    
    IKr = IK_SUCCESS;
    IK1 = llEuler2Rot(-axis * q1);
    IK2 = llEuler2Rot(-axis * q2);
}
// Return values:
integer IK; // IK results; IK_OUT_OF_REACH, IK_TOO_CLOSE, IK_SUCCESS
rotation IK1; // First joint rotation
rotation IK2; // Second joint rotation
