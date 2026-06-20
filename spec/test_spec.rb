describe 'database' do
    before do
        `rm -rf test.db`
    end

    def run_script(commands)
        raw_output = nil
        IO.popen("./main test.db", "r+") do |pipe|
            commands.each do |command|
                pipe.puts command
            end
            pipe.close_write
            raw_output = pipe.gets(nil)
        end
        raw_output.split("\n")
    end

    it 'inserts and retrieves a row' do
        result = run_script(["insert 1 foo x@gmail.com", "select", ".exit"])
        # db > Executed.\ndb > (1, foo, x@gmail.com)\nExecuted.\ndb >
        expect(result).to match_array([
            "db > Executed.", "db > (1, foo, x@gmail.com)", "Executed.", "db > "
        ])
    end

    it 'table is full' do 
        inserts = (1..1401).map do |n|
            "insert #{n} fill fill@gmail.com"
        end
        inserts << ".exit"
        result = run_script(inserts)

        expect(result[-2]).to eq("db > Table is full.")
    end

    it 'column limit' do
        username_limit = "u"*32
        email_limit = "e"*255
        result = run_script(["insert 1 #{username_limit} #{email_limit}", "select", ".exit"])
       
        expect(result).to match_array(["db > ", "db > Executed.", "Executed.", "db > (1, #{username_limit}, #{email_limit})"])
    end

    it 'over column limit' do
        username_over_limit = "u"*33
        email_over_limit = "e"*256

        result_username = run_script(["insert 1 #{username_over_limit} x@gmail.com", "select", ".exit"])
        result_email = run_script(["insert 1 x #{email_over_limit}", "select", ".exit"])

        expect(result_username).to match_array(["db > ", "db > Executed.", "db > Strings provided are to long"])
        expect(result_email).to match_array(["db > ", "db > Executed.", "db > Strings provided are to long"])
    end

    it 'negative id' do
        result = run_script(["insert -1 xe xe@gmail.com", ".exit"])
        expect(result).to match_array(["db > ", "db > Negative ID."])
    end

end