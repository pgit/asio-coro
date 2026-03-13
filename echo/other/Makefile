all: rust go client-go

prepare:
	sudo apt-get update && sudo apt-get -y install golang rustc

go: prepare
	go build -o echo-go echo.go

client-go: prepare
	go build -o client-go client.go

rust: prepare
	cargo build --release

clean:
	rm -rf target echo-go
