// A list global may be initialized from another list global, same as any other type.
list foo = [1, 2, 3];
list bar = foo;
// and the value has to propagate far enough that chains work too
list baz = bar;

// same deal when the global we're referencing has no rvalue of its own
list empty;
list empty_copy = empty;

default {
    state_entry() {
        llOwnerSay((string)baz + (string)empty_copy);
    }
}
