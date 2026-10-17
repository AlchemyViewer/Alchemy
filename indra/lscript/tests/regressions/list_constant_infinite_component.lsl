// A list constant's vectors and rotations are read back from the chunk the
// compiler wrote them to. A component not finite raised the VM's math
// fault there, which writes to a VM image's registers, past the end of the
// chunk. The compiler reads them itself now, and such a component is zero,
// as it was.
//
// expect said: <0.000000, 2.000000, 3.000000><1.000000, 2.000000, 0.000000, 4.000000>

list g = [<1e39, 2, 3>, <1, 2, 1e39, 4>];

default
{
    state_entry()
    {
        llSay(0, (string)g);
    }
}
