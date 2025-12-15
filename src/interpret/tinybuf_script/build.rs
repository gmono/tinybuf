fn main() {
    lalrpop::Configuration::new()
        .process_file("src/grammar.lalrpop")
        .expect("failed to process lalrpop grammar");
}
