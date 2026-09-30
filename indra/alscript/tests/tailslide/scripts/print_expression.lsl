default
{
    state_entry()
    {
        // print() is a (void) expression, not a statement, so it can be used in a forexpressionlist
        for(print("foo"),print("foo");0;)
        {
            ;
        }
    }
}
