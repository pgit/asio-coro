all: rust go

prepare:
	sudo apt-get update && sudo apt-get -y install golang rustc

go: prepare
	go build -o echo-go echo.go

rust: prepare
	cargo build --release

clean:
	rm -rf target echo-go
