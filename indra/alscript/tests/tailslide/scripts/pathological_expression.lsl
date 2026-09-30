default {
    state_entry() {
        integer i = 10;
        // Should be parsed as `i = i - (-(i--)) - i`
        llOwnerSay((string)(i-=-i---i)); // prints "29"

        // Should be parsed as `i = i - (-(i--)) - (i--)`
        llOwnerSay((string)(i-=-i---i--)); // prints "84"

        // Should be parsed as `i = i - (-(i--)) - (-(i--))`
        llOwnerSay((string)(i-=-i--- -i--)); // prints "81"

        // This is an error though, can't be parsed as
        // `i = i - (-(i--)) - (-(i--))` because the interpretation
        // becomes ambiguous without the space, is it a prefix
        // decrement like `--i`?
        llOwnerSay((string)(i-=-i----i--)); // $[E10020]
    }
}
