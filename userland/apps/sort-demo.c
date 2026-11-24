/* Bubble-sort and pointer traversal example for the Aiden C subset. */
void sort_values(int *items, int length) {
    for (int end = length - 1; end > 0; --end) {
        int changed = 0;
        for (int i = 0; i < end; ++i) {
            if (items[i] > items[i + 1]) {
                int temporary = items[i];
                items[i] = items[i + 1];
                items[i + 1] = temporary;
                changed = 1;
            }
        }
        if (!changed) break;
    }
}

int main(void) {
    int values[8] = { 42, 7, 19, 3, 88, 14, 1, 55 };
    sort_values(values, 8);
    puts("Sorted values: ");
    for (int i = 0; i < 8; ++i) printf("%d ", values[i]);
    puts("\n");
    return values[0] == 1 && values[7] == 88 ? 0 : 1;
}
