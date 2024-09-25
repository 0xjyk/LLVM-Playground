int fact(int n) {
    int ans = 1;
    while (n > 1) {
        ans *= n; 
        n--;
    }
    return ans;
}


int main(int argc, char **argv) {
    fact(3);   
}
