all: rust go

prepare:
	sudo apt-get update && sudo apt-get -y install golang rustc

clean:
	rm -rf target echo-go

#
# go
#
go: prepare
	go build -o echo-go echo.go

run-go:
	go run echo-go.go

#
# rust
#
rust: prepare
	cargo build --release

rust-run:
	cargo run
